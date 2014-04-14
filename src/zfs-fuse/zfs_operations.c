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

#include "fuse.h" //fuse_operations

#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/debug.h>
#include <sys/vnode.h>
#include <sys/cred_impl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/mode.h>
#include <sys/fcntl.h>

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#include "zfs_mounts.h"
#include "util.h"
#include "fuse_listener.h"
#include "dirent_engine.h"
#include "mounts_interface.h" //struct MountsPublicInterface



static struct MountsPublicInterface* s_toplevelfs;

/*the same as stat*/
static int op_getattr(const char *path, struct stat *st){
    int ret = s_toplevelfs->stat(s_toplevelfs, path, st);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_readlink(const char *path, char *buf, size_t bufsize){
    ssize_t ret = s_toplevelfs->readlink(s_toplevelfs, path, buf, bufsize);
    if ( ret > 0 ) return 0; //fuse: The return value should be 0 for success.
    else return -errno;
}

static int op_mknod(const char *path, mode_t mode, dev_t dev){
    int ret = s_toplevelfs->mknod(s_toplevelfs, path, mode, dev);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_mkdir(const char *path, mode_t mode){
    int ret = s_toplevelfs->mkdir(s_toplevelfs, path, mode);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_unlink(const char *path){
    int ret = s_toplevelfs->unlink(s_toplevelfs, path);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_rmdir(const char *path){
    int ret = s_toplevelfs->rmdir(s_toplevelfs, path);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_symlink(const char *oldpath, const char *newpath){
    int ret = s_toplevelfs->symlink(s_toplevelfs, oldpath, newpath);
    if ( ret == -1 ) return -errno;
    else return 0;
}
 
static int op_rename(const char *oldpath, const char *newpath){
    return s_toplevelfs->rename(s_toplevelfs, oldpath, newpath);
}
 
static int op_link(const char *oldpath, const char *newpath){
    int ret = s_toplevelfs->link(s_toplevelfs, oldpath, newpath);
    if ( ret == -1 ) return -errno;
    else return 0;
}
	
static int op_chmod(const char *path, mode_t mode){
    int ret = s_toplevelfs->chmod(s_toplevelfs, path, mode);
    if ( ret == -1 ) return -errno;
    else return 0;
}
 
static int op_chown(const char *path, uid_t owner, gid_t group){
    int ret = s_toplevelfs->chown(s_toplevelfs, path, owner, group);
    if ( ret == -1 ) return -errno;
    else return 0;
}
 
static int op_open(const char *path, struct fuse_file_info *fi){
    int fd = s_toplevelfs->open(s_toplevelfs, path, fi->flags, 0);
    if ( fd < 0 ){
        return -errno;
    }
    fi->fh = fd;
    return 0;
}
 
static int op_read(const char *path, char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi){
    int ret = s_toplevelfs->pread(s_toplevelfs, fi->fh, buf, bufsize, offset);
    if ( ret == -1 ) return -errno;
    else return 0;
}
 
static int op_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi){
    int ret = s_toplevelfs->pwrite(s_toplevelfs, fi->fh, buf, bufsize, offset);
    if ( ret == -1 ) return -errno;
    else return 0;
}
 
static int op_statvfs(const char *path, struct statvfs *buf){
    int ret = s_toplevelfs->statvfs(s_toplevelfs, path, buf);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_access(const char *path, int mode){
    int ret = s_toplevelfs->access(s_toplevelfs, path, mode);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_opendir(const char *path, struct fuse_file_info *fi){
    fi->flags |= O_DIRECTORY;
    fi->fh=s_toplevelfs->open(s_toplevelfs, path, fi->flags, 0);
    if ( fi->fh == -1 ) return -errno;
    else return fi->fh;
}
 
static int op_releasedir(const char *path, struct fuse_file_info *fi){
    int ret = s_toplevelfs->close(s_toplevelfs, fi->fh);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_release(const char *path, struct fuse_file_info *fi){
    int ret = s_toplevelfs->close(s_toplevelfs, fi->fh);
    if ( ret == -1 ) return -errno;
    else return 0;
}

static int op_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
    static char temp_buf[PATH_MAX];
    static int  temp_buf_off = sizeof(temp_buf);
    static int  getdents_buf_len = 0;
    unsigned long d_ino;
    unsigned long d_type;
    const char *d_name;
    int ret=0;
    struct DirentEnginePublicInterface *dirent_engine = INSTANCE_L(DIRENT_ENGINE)();

    /*handle already readed dirent in temp buffer OR read new by getdents call*/
    while( temp_buf_off < getdents_buf_len ||  
	   (getdents_buf_len=s_toplevelfs->getdents(s_toplevelfs, fi->fh, temp_buf, sizeof(temp_buf))) > 0 ){
	assert(getdents_buf_len<=sizeof(temp_buf));
	/*check if all info handled in temp_buf*/
	if ( temp_buf_off >= getdents_buf_len ){
	    temp_buf_off=0;
	}
	//extract item from getdents's buffer
	while( NULL != (d_name = dirent_engine
			->get_next_item_from_dirent_buf( temp_buf, //buf
							 getdents_buf_len, //bufsize
							 &temp_buf_off, //cursor
							 &d_ino, &d_type )) ){
#ifdef DEBUG
			printf("-----dir=%s\n", d_name);fflush(0);
#endif
	    //add item to fuse buffer by filler function
	    if ( filler(buf, d_name, NULL, 0) != 0 ){
		return -ENOMEM;
	    }
	}
	if ( d_name == NULL )
	    temp_buf_off = sizeof(temp_buf);
    }
    if ( getdents_buf_len == 0 ){
	temp_buf_off = sizeof(temp_buf);
    }
    else if (getdents_buf_len < 0)
	return -errno;

    return ret;
}
 
/* void *(* 	init )(struct fuse_conn_info *conn){ */
/* } */
 
/* void(* 	destroy )(void *){ */
/* } */
 
/* int(* 	create )(const char *, mode_t, struct fuse_file_info *){ */
/* } */
 
static int op_ftruncate(const char *path, off_t length, struct fuse_file_info *finfo){
    int ret = s_toplevelfs->ftruncate_size(s_toplevelfs, finfo->fh, length);
    if ( ret == -1 ) return errno;
    else return 0;
}

static int op_fgetattr(const char *path, struct stat *st, struct fuse_file_info *finfo){
    int ret = s_toplevelfs->fstat(s_toplevelfs, finfo->fh, st);
    if ( ret == -1 ) return errno;
    else return 0;
}

 
/* int(* 	lock )(const char *, struct fuse_file_info *, int cmd, struct flock *){ */
/* } */
 
/* int(* 	utimens )(const char *, const struct timespec tv[2]){ */
/* } */
 
/* int(* 	bmap )(const char *, size_t blocksize, uint64_t *idx){ */
/* } */
 
/* int(* 	ioctl )(const char *, int cmd, void *arg, struct fuse_file_info *, unsigned int flags, void *data){ */
/* } */
 
/* int(* 	poll )(const char *, struct fuse_file_info *, struct fuse_pollhandle *ph, unsigned *reventsp){ */
/* } */
 
/* int(* 	write_buf )(const char *, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *){ */
/* } */
 
/* int(* 	read_buf )(const char *, struct fuse_bufvec **bufp, size_t size, off_t off, struct fuse_file_info *){ */
/* } */
 
/* int(* 	flock )(const char *, struct fuse_file_info *, int op){ */
/* } */
 
/* int(* 	fallocate )(const char *, int, off_t, off_t, struct fuse_file_info *){ */
/* } */


struct fuse_operations s_zfs_operation = {
    .getattr  = op_getattr,  //ok stat
    .readlink = op_readlink, //added
    .mknod    = op_mknod,    //added
    .mkdir    = op_mkdir,    //ok
    .unlink   = op_unlink,   //ok
    .rmdir    = op_rmdir,    //ok
    .symlink  = op_symlink,  //added
    .rename   = op_rename,   //ZRT has not (added on zfs side)
    .link     = op_link,     //link
    .chmod    = op_chmod,    //ZFS-FUSE not support it
    .chown    = op_chown,    //ZFS-FUSE not support it
    //truncate, //not needed or NULL
    .open     = op_open,     //ok
    .read     = op_read,     //ok
    .write    = op_write,    //ok
    .statfs   = op_statvfs,   //not needed
    //flush,    //not needed or NULL
    .release  = op_release,//FUSE function
    //fsync,    //ZFS-FUSE has it, ZRT not
    //setxattr, //not needed
    //getxattr, //not needed
    //listxattr,//not needed 
    //removexattr,//not needed
    .opendir  = op_opendir,  //ZFS-FUSE has, ZRT not
    .readdir  = op_readdir,  //ZFS-FUSE has, ZRT has not
    .releasedir= op_releasedir,//FUSE function
    //fsyncdir, //ZRT, ZFS-FUSE has not
    //init,     //ZRT has not
    //destroy,  //ZFS-FUSE has, ZRT has not, not needed
    .access   = op_access,   //ZRT has not, now has
    //create,   //ZRT has not
    .ftruncate= op_ftruncate,//ZFS-FUSE has & ZRT has
    .fgetattr = op_fgetattr, //ZRT has not
    //lock,     //ZRT has not
    //utimens,  //ZRT has not
    //bmap,     //ZRT has not
    //ioctl,    //ZRT has not
    //poll,     //ZRT has not, not needed
    //write_buf,//ZRT has not, not needed
    //read_buf, //ZRT has not, not needed
    //flock,    //ZRT has not
    //fallocate //ZRT has not
};


struct fuse_operations* fuse_operations_construct(vfs_t *vfs){
    struct MountsPublicInterface* zfs_mounts = CONSTRUCT_L(ZFS_MOUNTS)(vfs);
    s_toplevelfs = zfs_mounts;
    assert(s_toplevelfs);
    /*just get static array of functions, it is expected that them will be use
     s_toplevelfs object to provide implementation*/
    return &s_zfs_operation;
}

struct fuse_operations* get_zfs_operations(){
    return &s_zfs_operation;
}

