/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Ricardo Correia.
 * Portions Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * ZFS filesystem interface implementation
 *
 * Copyright (c) 2014, LiteStack, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this_ file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>

#include "zfs_filesystem.h"
#include "open_file_description.h"
#include "lowlevel_filesystem.h"
#include "dirent_engine.h"

#include <sys/mode.h> //VTTOIF
#include <sys/zfs_vfsops.h> //zfsvfs_t
#include <sys/zfs_znode.h> //ZFS_ENTER
#include <sys/fcntl.h> //flock64_t
#include <sys/statvfs.h> //statvfs64
#include <sys/cred_impl.h> //struct cred, cred_t
#include <sys/cred.h> //cred_t


#define INVERT_SIGN( errcode ) -(errcode)

struct ZfsFilesystem{
    struct LowLevelFilesystemPublicInterface public_;
    vfs_t *vfs;
};

static off_t s_zfs_dirent_off;
static cred_t s_cred;

static void get_zfs_flags_from_standard_open_flags_mode( int std_flags, 
							 int *zfs_flags, int *zfs_mode ){
    if(std_flags & O_WRONLY) {
	*zfs_mode = VWRITE;
	*zfs_flags = FWRITE;
    } else if(std_flags & O_RDWR) {
	*zfs_mode = VREAD | VWRITE;
	*zfs_flags = FREAD | FWRITE;
    } else {
	*zfs_mode = VREAD;
	*zfs_flags = FREAD;
    }

    if(std_flags & O_CREAT)
	*zfs_flags |= FCREAT;
    if(std_flags & O_SYNC)
	*zfs_flags |= FSYNC;
    if(std_flags & O_DSYNC)
	*zfs_flags |= FDSYNC;
    if(std_flags & O_RSYNC)
	*zfs_flags |= FRSYNC;
    if(std_flags & O_APPEND)
	*zfs_flags |= FAPPEND;
    if(std_flags & O_LARGEFILE)
	*zfs_flags |= FOFFMAX;
    if(std_flags & O_NOFOLLOW)
	*zfs_flags |= FNOFOLLOW;
    if(std_flags & O_TRUNC)
	*zfs_flags |= FTRUNC;
    if(std_flags & O_EXCL)
	*zfs_flags |= FEXCL;
}


static int internal_stat(vnode_t *vp, struct stat *stbuf)
{
	ASSERT(vp != NULL);
	ASSERT(stbuf != NULL);

	vattr_t vattr;
	vattr.va_mask = AT_STAT | AT_NBLOCKS | AT_BLKSIZE | AT_SIZE;

	cred_t *cred = &s_cred;
	int error = VOP_GETATTR(vp, &vattr, 0, cred, NULL);
	if(error)
	    return INVERT_SIGN(error);

	memset(stbuf, 0, sizeof(struct stat));

	stbuf->st_dev = vattr.va_fsid;
	stbuf->st_ino = vattr.va_nodeid == 3 ? 1 : vattr.va_nodeid;
	stbuf->st_mode = VTTOIF(vattr.va_type) | vattr.va_mode;
	stbuf->st_nlink = vattr.va_nlink;
	stbuf->st_uid = vattr.va_uid;
	stbuf->st_gid = vattr.va_gid;
	stbuf->st_rdev = vattr.va_rdev;
	stbuf->st_size = vattr.va_size;
	stbuf->st_blksize = vattr.va_blksize;
	stbuf->st_blocks = vattr.va_nblocks;
	TIMESTRUC_TO_TIME(vattr.va_atime, &stbuf->st_atime);
	TIMESTRUC_TO_TIME(vattr.va_mtime, &stbuf->st_mtime);
	TIMESTRUC_TO_TIME(vattr.va_ctime, &stbuf->st_ctime);

	return 0;
}

static int zfs_lookup(struct LowLevelFilesystemPublicInterface* this_,
		  int parent_inode, const char *name)
{
	if(strlen(name) >= MAXNAMELEN)
	    return INVERT_SIGN(ENAMETOOLONG);
	int retinode;
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *znode;

	int error = zfs_zget(zfsvfs, parent_inode, &znode, B_FALSE);
	if(error) {
		ZFS_EXIT(zfsvfs);
		/* If the inode we are trying to get was recently deleted
		   dnode_hold_impl will return EEXIST instead of ENOENT */
		return error == EEXIST ? ENOENT : error;
	}

	ASSERT(znode != NULL);
	vnode_t *dvp = ZTOV(znode);
	ASSERT(dvp != NULL);

	vnode_t *vp = NULL;
	cred_t *cred = &s_cred;

	error = VOP_LOOKUP(dvp, (char *) name, &vp, NULL, 0, NULL, cred, NULL, NULL, NULL);
	if(error)
		goto out;

	if(vp == NULL)
		goto out;

	retinode = VTOZ(vp)->z_id;
	struct stat st;
	error = internal_stat(vp, &st);
	if ( error == 0 ) return retinode;

out:
	if(vp != NULL)
		VN_RELE(vp);
	VN_RELE(dvp);
	ZFS_EXIT(zfsvfs);

	return INVERT_SIGN(error);
}


static int zfs_statvfs(struct LowLevelFilesystemPublicInterface* this_, struct statvfs *stat)
{
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;
	vfs_t *vfs = zfs->vfs;

	struct statvfs64 zfs_stat;

	int ret = VFS_STATVFS(vfs, &zfs_stat);
	if(ret != 0) {
		return -1;
	}

	stat->f_bsize = zfs_stat.f_bsize;
	stat->f_frsize = zfs_stat.f_frsize;
	stat->f_blocks = zfs_stat.f_blocks;
	stat->f_bfree = zfs_stat.f_bfree;
	stat->f_bavail = zfs_stat.f_bavail;
	stat->f_files = zfs_stat.f_files;
	stat->f_ffree = zfs_stat.f_ffree;
	stat->f_favail = zfs_stat.f_favail;
	stat->f_fsid = zfs_stat.f_fsid;
	stat->f_flag = zfs_stat.f_flag;
	stat->f_namemax = zfs_stat.f_namemax;
	
	return 0;
}


static int zfs_stat(struct LowLevelFilesystemPublicInterface* this_, 
		    ino_t inode, struct stat *buf){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_TRUE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return error == EEXIST ? INVERT_SIGN(ENOENT) : INVERT_SIGN(error);
	}

	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);

	error = internal_stat(vp, buf);

	VN_RELE(vp);
	ZFS_EXIT(zfsvfs);
	return INVERT_SIGN(error);
}

static int zfs_mkdir(struct LowLevelFilesystemPublicInterface* this_, 
		     ino_t parent_inode, const char* name, uint32_t mode){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	if(strlen(name) >= MAXNAMELEN)
	    return INVERT_SIGN(ENAMETOOLONG);

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *znode;

	int error = zfs_zget(zfsvfs, parent_inode, &znode, B_FALSE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return error == EEXIST ? INVERT_SIGN(ENOENT) : INVERT_SIGN(error);
	}

	ASSERT(znode != NULL);
	vnode_t *dvp = ZTOV(znode);
	ASSERT(dvp != NULL);

	vnode_t *vp = NULL;

	vattr_t vattr = { 0 };
	vattr.va_type = VDIR;
	vattr.va_mode = mode & PERMMASK;
	vattr.va_mask = AT_TYPE | AT_MODE;

	cred_t* cred = &s_cred;
	error = VOP_MKDIR(dvp, (char *) name, &vattr, &vp, cred, NULL, 0, NULL);
	if(error)
	    goto out;

	ASSERT(vp != NULL);

	struct stat st;
	error = internal_stat(vp, &st);

 out:
	if(vp != NULL)
	    VN_RELE(vp);
	VN_RELE(dvp);
	ZFS_EXIT(zfsvfs);

	if ( error == 0 ) 
	    return VTOZ(vp)->z_id; //inode
	else
	    return INVERT_SIGN(error);
}

static int zfs_rmdir(struct LowLevelFilesystemPublicInterface* this_, 
		     ino_t parent_inode, const char* name){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	if(strlen(name) >= MAXNAMELEN)
	    return INVERT_SIGN(ENAMETOOLONG);

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *znode;

	int error = zfs_zget(zfsvfs, parent_inode, &znode, B_FALSE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return error == EEXIST ? ENOENT : error;
	}

	ASSERT(znode != NULL);
	vnode_t *dvp = ZTOV(znode);
	ASSERT(dvp != NULL);

	cred_t *cred = &s_cred;

	/* FUSE doesn't care if we remove the current working directory
	   so we just pass NULL as the cwd parameter (no problem for ZFS) */
	error = VOP_RMDIR(dvp, (char *) name, NULL, cred, NULL, 0);

	/* Linux uses ENOTEMPTY when trying to remove a non-empty directory */
	if(error == EEXIST)
	    error = ENOTEMPTY;

	VN_RELE(dvp);
	ZFS_EXIT(zfsvfs);

	return INVERT_SIGN(error);
}

static ssize_t zfs_pread(struct LowLevelFilesystemPublicInterface* this_,
			 ino_t inode, void *buf, size_t size, off_t offset){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);
	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_TRUE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return error == EEXIST ? INVERT_SIGN(ENOENT) : INVERT_SIGN(error);
	}
	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);

	iovec_t iovec;
	uio_t uio;
	uio.uio_iov = &iovec;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_llimit = RLIM64_INFINITY;

	iovec.iov_base = buf;
	iovec.iov_len = size;
	uio.uio_resid = iovec.iov_len;
	uio.uio_loffset = offset;

	cred_t *cred = &s_cred;

	/*flags can't be checked here because flag can't be
	  represented by inode, so do test skiping by specifying valid
	  flag*/
	error = VOP_READ(vp, &uio, O_RDONLY, cred, NULL);

	ZFS_EXIT(zfsvfs);

	if ( uio.uio_loffset - offset >=0 && error == 0 ) 
	    return uio.uio_loffset - offset; //readed bytes

	return INVERT_SIGN(error);
}

static ssize_t zfs_pwrite(struct LowLevelFilesystemPublicInterface* this_,
			  ino_t inode, const void *buf, size_t size, off_t offset){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);
	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_TRUE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return error == EEXIST ? INVERT_SIGN(ENOENT) : INVERT_SIGN(error);
	}
	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);

	iovec_t iovec;
	uio_t uio;
	uio.uio_iov = &iovec;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_llimit = RLIM64_INFINITY;

	iovec.iov_base = (void *) buf;
	iovec.iov_len = size;
	uio.uio_resid = iovec.iov_len;
	uio.uio_loffset = offset;

	cred_t *cred = &s_cred;
	/*flags can't be checked here because flag can't be
	  represented by inode, so do test skiping by specifying valid
	  flag*/
	error = VOP_WRITE(vp, &uio, O_WRONLY, cred, NULL);

	ZFS_EXIT(zfsvfs);

	if(!error) {
	    /* When not using direct_io, we must always write 'size' bytes */
	    VERIFY(uio.uio_resid == 0);
	}

	if ( uio.uio_loffset - offset >=0 && error == 0 ) 
	    return uio.uio_loffset - offset; //wrote bytes

	return INVERT_SIGN(error);
}


static int zfs_getdents(struct LowLevelFilesystemPublicInterface* this_, 
			ino_t inode, void *buf, unsigned int count, off_t offset,
			int *lastcall_workaround){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);
	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_TRUE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return error == EEXIST ? INVERT_SIGN(ENOENT) : INVERT_SIGN(error);
	}
	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);

	if(vp->v_type != VDIR)
	    return INVERT_SIGN(ENOTDIR);

	ZFS_ENTER(zfsvfs);

	cred_t *cred = &s_cred;

	iovec_t iovec;
	uio_t uio;
	uio.uio_iov = &iovec;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_llimit = RLIM64_INFINITY;

	int eofp = 0;

	int outbuf_off = 0;
	int outbuf_resid = count;
	off_t next = offset;
	
	iovec.iov_base = buf;
	iovec.iov_len = count;
	uio.uio_resid = iovec.iov_len;
	uio.uio_loffset = next;

	error = VOP_READDIR(vp, &uio, cred, &eofp, NULL, 0);
	*lastcall_workaround = eofp;
	if(error)
	    goto out;

 out:
	ZFS_EXIT(zfsvfs);

	if ( uio.uio_loffset > 0 )
	    return iovec.iov_base - buf;
	else
	    return INVERT_SIGN(error);
}

static int zfs_opendir(struct LowLevelFilesystemPublicInterface* this_, ino_t inode)
{
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);
	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_TRUE);
	if(error) {
		ZFS_EXIT(zfsvfs);
		/* If the inode we are trying to get was recently deleted
		   dnode_hold_impl will return EEXIST instead of ENOENT */
		return error == EEXIST ? INVERT_SIGN(ENOENT) : INVERT_SIGN(error);
	}

	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);

	if(vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}

	cred_t *cred = &s_cred;

	/*
	 * Check permissions.
	 */
	if (error = VOP_ACCESS(vp, VREAD | VEXEC, 0, cred, NULL))
		goto out;

	vnode_t *old_vp = vp;

	/* XXX: not sure about flags */
	error = VOP_OPEN(&vp, FREAD, cred, NULL);

	ASSERT(old_vp == vp);
out:
	if(error)
		VN_RELE(vp);
	ZFS_EXIT(zfsvfs);

	return INVERT_SIGN(error);
}


static int zfs_close(struct LowLevelFilesystemPublicInterface* this_, ino_t inode, int fflags){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);
	ZFS_ENTER(zfsvfs);
	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_TRUE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}
	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);


	int mode, flags;
	get_zfs_flags_from_standard_open_flags_mode( fflags, &flags, &mode );

	cred_t *cred = &s_cred;
	VOP_CLOSE(vp, flags, 1, (offset_t) 0, cred, NULL);

	VERIFY(error == 0);

	VN_RELE(vp);

	ZFS_EXIT(zfsvfs);

	return INVERT_SIGN(error);
}
static int zfs_open(struct LowLevelFilesystemPublicInterface* this_, 
		    ino_t parent_inode, const char* name, int fflags, uint32_t createmode){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	if ( fflags & O_DIRECTORY )
	    return zfs_opendir(this_, parent_inode);
	else{
	    if(name && strlen(name) >= MAXNAMELEN)
		return INVERT_SIGN(ENAMETOOLONG);

	    vfs_t *vfs = zfs->vfs;
	    zfsvfs_t *zfsvfs = vfs->vfs_data;

	    ZFS_ENTER(zfsvfs);

	    cred_t *cred = &s_cred;

	    /* Map flags */
	    int mode, flags;
	    get_zfs_flags_from_standard_open_flags_mode( fflags, &flags, &mode );

	    znode_t *znode;

	    int error = zfs_zget(zfsvfs, parent_inode, &znode, B_FALSE);
	    if(error) {
		ZFS_EXIT(zfsvfs);
		/* If the inode we are trying to get was recently deleted
		   dnode_hold_impl will return EEXIST instead of ENOENT */
		return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	    }

	    ASSERT(znode != NULL);
	    vnode_t *vp = ZTOV(znode);
	    ASSERT(vp != NULL);

	    if (flags & FCREAT) {
		enum vcexcl excl;

		/*
		 * Wish to create a file.
		 */
		vattr_t vattr;
		vattr.va_type = VREG;
		vattr.va_mode = mode;
		vattr.va_mask = AT_TYPE|AT_MODE;
		if (flags & FTRUNC) {
		    vattr.va_size = 0;
		    vattr.va_mask |= AT_SIZE;
		}
		if (flags & FEXCL)
		    excl = EXCL;
		else
		    excl = NONEXCL;

		vnode_t *new_vp;
		/* FIXME: check filesystem boundaries */
		error = VOP_CREATE(vp, (char *) name, &vattr, excl, mode, &new_vp, cred, 0, NULL, NULL);

		if(error)
		    goto out;

		VN_RELE(vp);
		vp = new_vp;
	    } else {
		/*
		 * Get the attributes to check whether file is large.
		 * We do this only if the O_LARGEFILE flag is not set and
		 * only for regular files.
		 */
		if (!(flags & FOFFMAX) && (vp->v_type == VREG)) {
		    vattr_t vattr;
		    vattr.va_mask = AT_SIZE;
		    if ((error = VOP_GETATTR(vp, &vattr, 0, cred, NULL)))
			goto out;

		    if (vattr.va_size > (u_offset_t) MAXOFF32_T) {
			/*
			 * Large File API - regular open fails
			 * if FOFFMAX flag is set in file mode
			 */
			error = EOVERFLOW;
			goto out;
		    }
		}

		/*
		 * Check permissions.
		 */
		if (error = VOP_ACCESS(vp, mode, 0, cred, NULL))
		    goto out;
	    }

	    if ((flags & FNOFOLLOW) && vp->v_type == VLNK) {
		error = ELOOP;
		goto out;
	    }

	    vnode_t *old_vp = vp;

	    error = VOP_OPEN(&vp, flags, cred, NULL);

	    ASSERT(old_vp == vp);

	    if(error)
		goto out;

	    struct stat st;

	    if(flags & FCREAT) {
		error = internal_stat(vp, &st);
		if(error)
		    goto out;
	    }

	out:
	    if(error) {
		ASSERT(vp->v_count > 0);
		VN_RELE(vp);
	    }

	    ZFS_EXIT(zfsvfs);

	    if (flags & FCREAT)
		return VTOZ(vp)->z_id;
	    else
		return INVERT_SIGN(error);
	}
}

static ssize_t zfs_readlink(struct LowLevelFilesystemPublicInterface* this_, 
			    ino_t inode, char *buf, size_t bufsize)
{
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;
	
	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_FALSE);
	if(error) {
		ZFS_EXIT(zfsvfs);
		/* If the inode we are trying to get was recently deleted
		   dnode_hold_impl will return EEXIST instead of ENOENT */
		return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}

	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);

	iovec_t iovec;
	uio_t uio;
	uio.uio_iov = &iovec;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_llimit = RLIM64_INFINITY;
	iovec.iov_base = buf;
	iovec.iov_len = bufsize - 1;
	uio.uio_resid = iovec.iov_len;
	uio.uio_loffset = 0;

	cred_t * cred = &s_cred;
	error = VOP_READLINK(vp, &uio, cred, NULL);

	VN_RELE(vp);
	ZFS_EXIT(zfsvfs);

	if(!error) {
		VERIFY(uio.uio_loffset < bufsize);
		buf[uio.uio_loffset] = '\0';
		return uio.uio_loffset;
	}
	else
	    return INVERT_SIGN(error);
}


static int zfs_symlink(struct LowLevelFilesystemPublicInterface* this_, 
		       const char *link, ino_t parent_inode, const char *name)
{
	if(strlen(name) >= MAXNAMELEN)
	    return INVERT_SIGN(ENAMETOOLONG);

	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *znode;

	int error = zfs_zget(zfsvfs, parent_inode, &znode, B_FALSE);
	if(error) {
		ZFS_EXIT(zfsvfs);
		/* If the inode we are trying to get was recently deleted
		   dnode_hold_impl will return EEXIST instead of ENOENT */
		return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}

	ASSERT(znode != NULL);
	vnode_t *dvp = ZTOV(znode);
	ASSERT(dvp != NULL);

	cred_t *cred = &s_cred;

	vattr_t vattr;
	vattr.va_type = VLNK;
	vattr.va_mode = 0777;
	vattr.va_mask = AT_TYPE | AT_MODE;

	error = VOP_SYMLINK(dvp, (char *) name, &vattr, (char *) link, cred, NULL, 0);

	vnode_t *vp = NULL;

	if(error)
		goto out;

	error = VOP_LOOKUP(dvp, (char *) name, &vp, NULL, 0, NULL, cred, NULL, NULL, NULL);
	if(error)
		goto out;

	ASSERT(vp != NULL);

	struct stat st;
	error = internal_stat(vp, &st);

out:
	if(vp != NULL)
		VN_RELE(vp);
	VN_RELE(dvp);

	ZFS_EXIT(zfsvfs);

	if ( error == 0 )
	    return VTOZ(vp)->z_id;
	else
	    return INVERT_SIGN(error);
}


static int zfs_unlink(struct LowLevelFilesystemPublicInterface* this_, 
		      ino_t parent_inode, const char* name){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;
	
	if(strlen(name) >= MAXNAMELEN)
	    return INVERT_SIGN(ENAMETOOLONG);

	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *znode;

	int error = zfs_zget(zfsvfs, parent_inode, &znode, B_FALSE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}

	ASSERT(znode != NULL);
	vnode_t *dvp = ZTOV(znode);
	ASSERT(dvp != NULL);

	cred_t *cred = &s_cred;
	error = VOP_REMOVE(dvp, (char *) name, cred, NULL, 0);

	VN_RELE(dvp);
	ZFS_EXIT(zfsvfs);

	return INVERT_SIGN(error);
}
static int zfs_link(struct LowLevelFilesystemPublicInterface* this_, 
		    ino_t inode, ino_t new_parent, const char *newname){
	if(strlen(newname) >= MAXNAMELEN)
	    return INVERT_SIGN(ENAMETOOLONG);

	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;
	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *td_znode, *s_znode;

	int error = zfs_zget(zfsvfs, inode, &s_znode, B_FALSE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}

	ASSERT(s_znode != NULL);

	error = zfs_zget(zfsvfs, new_parent, &td_znode, B_FALSE);
	if(error) {
	    VN_RELE(ZTOV(s_znode));
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return error == EEXIST ? ENOENT : error;
	}

	vnode_t *svp = ZTOV(s_znode);
	vnode_t *tdvp = ZTOV(td_znode);
	ASSERT(svp != NULL);
	ASSERT(tdvp != NULL);

	cred_t *cred = &s_cred;
	error = VOP_LINK(tdvp, svp, (char *) newname, cred, NULL, 0);

	vnode_t *vp = NULL;

	if(error)
	    goto out;

	error = VOP_LOOKUP(tdvp, (char *) newname, &vp, NULL, 0, NULL, cred, NULL, NULL, NULL);
	if(error)
	    goto out;

	ASSERT(vp != NULL);

	struct stat st;
	error = internal_stat(vp, &st);

 out:
	if(vp != NULL)
	    VN_RELE(vp);
	VN_RELE(tdvp);
	VN_RELE(svp);

	ZFS_EXIT(zfsvfs);

	if ( error == 0 )
	    return VTOZ(vp)->z_id;
	else
	    return INVERT_SIGN(error);
}


static int zfs_access(struct LowLevelFilesystemPublicInterface* this_, 
		      ino_t inode, int mask){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;
	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_TRUE);
	if(error) {
		ZFS_EXIT(zfsvfs);
		/* If the inode we are trying to get was recently deleted
		   dnode_hold_impl will return EEXIST instead of ENOENT */
		return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}

	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);

	cred_t *cred = &s_cred;

	int mode = 0;
	if(mask & R_OK)
		mode |= VREAD;
	if(mask & W_OK)
		mode |= VWRITE;
	if(mask & X_OK)
		mode |= VEXEC;

	error = VOP_ACCESS(vp, mode, 0, cred, NULL);

	VN_RELE(vp);

	ZFS_EXIT(zfsvfs);

	return INVERT_SIGN(error);
}



static int zfs_rename(struct LowLevelFilesystemPublicInterface* this_, 
		      ino_t parent, const char *name,
		      ino_t new_parent, const char *newname){
	if(strlen(name) >= MAXNAMELEN || strlen(newname) >= MAXNAMELEN )
	    return INVERT_SIGN(ENAMETOOLONG);

	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;
	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);

	znode_t *p_znode, *np_znode;

	int error = zfs_zget(zfsvfs, parent, &p_znode, B_FALSE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}

	ASSERT(p_znode != NULL);

	error = zfs_zget(zfsvfs, new_parent, &np_znode, B_FALSE);
	if(error) {
	    VN_RELE(ZTOV(p_znode));
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}

	ASSERT(np_znode != NULL);

	vnode_t *p_vp = ZTOV(p_znode);
	vnode_t *np_vp = ZTOV(np_znode);
	ASSERT(p_vp != NULL);
	ASSERT(np_vp != NULL);

	cred_t *cred = &s_cred;
	error = VOP_RENAME(p_vp, (char *) name, np_vp, (char *) newname, cred, NULL, 0);

	VN_RELE(p_vp);
	VN_RELE(np_vp);

	ZFS_EXIT(zfsvfs);

	return INVERT_SIGN(error);
}
static int zfs_ftruncate_size(struct LowLevelFilesystemPublicInterface* this_, 
			      ino_t inode, off_t length){
	struct ZfsFilesystem* zfs = (struct ZfsFilesystem*)this_;
	vfs_t *vfs = zfs->vfs;
	zfsvfs_t *zfsvfs = vfs->vfs_data;

	ZFS_ENTER(zfsvfs);
	ZFS_ENTER(zfsvfs);
	znode_t *znode;

	int error = zfs_zget(zfsvfs, inode, &znode, B_TRUE);
	if(error) {
	    ZFS_EXIT(zfsvfs);
	    /* If the inode we are trying to get was recently deleted
	       dnode_hold_impl will return EEXIST instead of ENOENT */
	    return INVERT_SIGN(error == EEXIST ? ENOENT : error);
	}
	ASSERT(znode != NULL);
	vnode_t *vp = ZTOV(znode);
	ASSERT(vp != NULL);

	cred_t *cred = &s_cred;
	boolean_t release;
	int flags = FWRITE; 
	/*Flags did checked on toplevelfs, and real flags checking
	 *will be skipped, we just set valid flag*/
	{
	    release = B_FALSE;

	    /*
	     * Special treatment for ftruncate().
	     * This is needed because otherwise ftruncate() would
	     * fail with permission denied on read-only files.
	     * (Solaris calls VOP_SPACE instead of VOP_SETATTR on
	     * ftruncate).
	     */
	    /* Sanity check */
	    if(vp->v_type != VREG) {
		error = EINVAL;
		goto out;
	    }

	    flock64_t bf;

	    bf.l_whence = 0; /* beginning of file */
	    bf.l_start = length;
	    bf.l_type = F_WRLCK;
	    bf.l_len = (off_t) 0;

	    /* FIXME: check locks */
	    error = VOP_SPACE(vp, F_FREESP, &bf, flags, 0, cred, NULL);
	    if(error)
		goto out;
	}

	ASSERT(vp != NULL);

	vattr_t vattr = { 0 };

	vattr.va_mask = AT_SIZE;
	vattr.va_size = length;

	error = VOP_SETATTR(vp, &vattr, flags, cred, NULL);

 out: ;
	struct stat stat_reply;

	if(!error)
	    error = internal_stat(vp, &stat_reply);

	/* Do not release if vp was an opened inode */
	if(release)
	    VN_RELE(vp);

	ZFS_EXIT(zfsvfs);

	return INVERT_SIGN(error);
}


static struct LowLevelFilesystemPublicInterface s_zfs_filesystem_interface = {
    zfs_lookup,
    zfs_readlink,
    zfs_symlink,
    NULL, //chown
    NULL, //chmod
    zfs_statvfs,
    zfs_stat,
    zfs_access,
    zfs_mkdir,
    zfs_rmdir,
    zfs_pread,
    zfs_pwrite,
    zfs_getdents,
    NULL, //fsync
    zfs_close,
    zfs_open,
    zfs_unlink,
    zfs_link,
    zfs_rename,
    zfs_ftruncate_size,
    NULL
};


struct LowLevelFilesystemPublicInterface* 
CONSTRUCT_L(ZFS_FILESYSTEM) (vfs_t *vfs, 
			  struct DirentEnginePublicInterface* dirent_engine){
	struct ZfsFilesystem* this_ = malloc(sizeof(struct ZfsFilesystem));
	this_->public_ = s_zfs_filesystem_interface;
	this_->public_.dirent_engine = dirent_engine;
	this_->vfs = vfs;
	return &this_->public_;
}
