/*
 *
 * Copyright (c) 2014, LiteStack, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
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


#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>

extern "C" {
#include "zrtlog.h"
#include "zrt_helper_macros.h"
}
#include "MemMount.h"

#ifdef __native_client__
#include "nacl-mounts/util/Path.h"
#endif //__native_client__

#include "lowlevel_filesystem.h"
#include "zfs_toplevel_filesystem.h"
#include "mounts_interface.h"

extern "C" {
#include "handle_allocator.h" //struct HandleAllocator, struct HandleItem
#include "open_file_description.h" //struct OpenFilesPool, struct OpenFileDescription
#include "dirent_engine.h"
#include "cached_lookup.h"
}

#define GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry_p)	\
    entry_p = fs->handle_allocator->entry( (fd) );	\
    if ( entry == NULL ){				\
	SET_ERRNO(EBADF);				\
	return -1;					\
    }

#define GET_INODE_ENSURE_EXIST(fs, path, inode_p)				\
    if ( (*(inode_p)=fs->cached_lookup->inode_by_path((fs)->cached_lookup, (path)) ) == -1 ){ \
	SET_ERRNO(ENOENT);						\
	return -1;							\
    }

#define GET_PARENT_ENSURE_EXIST(fs, path, inode_p)				\
    if ( (*(inode_p)=fs->cached_lookup->inode_by_path((fs)->cached_lookup, (path)) ) == -1 ){ \
	SET_ERRNO(ENOENT);						\
	return -1;							\
    }

#define CHECK_FUNC_ENSURE_EXIST(fs, funcname)	\
    if ( (fs)->lowlevelfs->funcname == NULL ){	\
	SET_ERRNO(ENOSYS);			\
	return -1;				\
    }


struct ZfsTopLevelFs{
    struct MountsPublicInterface public_;
    struct HandleAllocator* handle_allocator;
    struct OpenFilesPool*   open_files_pool;
    struct CachedLookupPublicInterface* cached_lookup;
    struct LowLevelFilesystemPublicInterface* lowlevelfs;
    MemMount*               mem_mount_cpp;
    struct MountSpecificPublicInterface* mount_specific_interface;
};


static const char* name_from_path( std::string path ){
    /*retrieve directory name, and compare name length with max available*/
    size_t pos=path.rfind ( '/' );
    int namelen = 0;
    if ( pos != std::string::npos ){
	namelen = path.length() -(pos+1);
	return path.c_str()+path.length()-namelen;
    }
    return NULL;
}

static int is_dir( struct LowLevelFilesystemPublicInterface* this_, ino_t inode ){
    struct stat st;
    int ret = this_->stat( this_, inode, &st );
    assert( ret == 0 );
    if ( S_ISDIR(st.st_mode) )
	return 1;
    else
	return 0;
}

/*wraper implementation
 All checks on zfs top level will be skipped, because it does in lowlevel*/

static ssize_t toplevel_readlink(struct MountsPublicInterface* this_,
				 const char *path, char *buf, size_t bufsize){
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int inode;
    int ret = -1;

    CHECK_FUNC_ENSURE_EXIST(fs, readlink);
    GET_INODE_ENSURE_EXIST(fs, path, &inode);

    if ( (ret=fs->lowlevelfs->readlink(fs->lowlevelfs, inode, buf, bufsize)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}

static int toplevel_symlink(struct MountsPublicInterface* this_,
			    const char *oldpath, const char *newpath){
    int ret=-1;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int new_inode_parent;

    CHECK_FUNC_ENSURE_EXIST(fs, symlink);
    GET_PARENT_ENSURE_EXIST(fs, newpath, &new_inode_parent);

    if (oldpath == NULL || !strlen(oldpath)) {
	SET_ERRNO(ENOENT);
        return -1;
    }

    const char* name = name_from_path( newpath);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( (ret=fs->lowlevelfs->symlink(fs->lowlevelfs, oldpath, new_inode_parent, name)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}



static int toplevel_chown(struct MountsPublicInterface* this_, const char* path, uid_t owner, gid_t group){
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int inode;
    int ret = -1;

    CHECK_FUNC_ENSURE_EXIST(fs, chown);
    GET_INODE_ENSURE_EXIST(fs, path, &inode);

    if ( (ret=fs->lowlevelfs->chown(fs->lowlevelfs, inode, owner, group)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}

static int toplevel_chmod(struct MountsPublicInterface* this_, const char* path, uint32_t mode){
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int inode;
    int ret = -1;

    CHECK_FUNC_ENSURE_EXIST(fs, chmod);
    GET_INODE_ENSURE_EXIST(fs, path, &inode);

    if ( (ret=fs->lowlevelfs->chmod(fs->lowlevelfs, inode, mode)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}

static int toplevel_statvfs(struct MountsPublicInterface* this_, const char* path, struct statvfs *buf){
    /*currently path is not used, but it will be used to choose
      appropriate lowlevel fs corresponding to path.*/
    int ret=-1;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;

    CHECK_FUNC_ENSURE_EXIST(fs, statvfs);

    if ( (ret=fs->lowlevelfs->statvfs(fs->lowlevelfs, buf)) < 0){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}


static int toplevel_stat(struct MountsPublicInterface* this_, const char* path, struct stat *buf){
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int inode;
    int ret = -1;

    CHECK_FUNC_ENSURE_EXIST(fs, stat);
    GET_INODE_ENSURE_EXIST(fs, path, &inode);

    if ( (ret=fs->lowlevelfs->stat(fs->lowlevelfs, inode, buf)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}

static int toplevel_mknod(struct MountsPublicInterface* this_, 
			  const char* path, mode_t mode, dev_t dev){
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int parent_inode;
    int ret = -1;

    CHECK_FUNC_ENSURE_EXIST(fs, mknod);
    GET_PARENT_ENSURE_EXIST(fs, path, &parent_inode);

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( (ret=fs->lowlevelfs->mknod(fs->lowlevelfs, parent_inode, name, mode, dev)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}

static int toplevel_mkdir(struct MountsPublicInterface* this_, const char* path, uint32_t mode){
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int parent_inode;
    int ret=-1;

    CHECK_FUNC_ENSURE_EXIST(fs, mkdir);
    GET_PARENT_ENSURE_EXIST(fs, path, &parent_inode);

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( (ret = fs->lowlevelfs->mkdir(fs->lowlevelfs, parent_inode, name, mode)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}


static int toplevel_rmdir(struct MountsPublicInterface* this_, const char* path){
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int parent_inode;
    int ret;

    CHECK_FUNC_ENSURE_EXIST(fs, rmdir);
    GET_PARENT_ENSURE_EXIST(fs, path, &parent_inode);

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( (ret = fs->lowlevelfs->rmdir(fs->lowlevelfs, parent_inode, name)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}

static ssize_t toplevel_read(struct MountsPublicInterface* this_, int fd, void *buf, size_t nbytes){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    CHECK_FUNC_ENSURE_EXIST(fs, pread);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( !CHECK_FLAG(ofd->flags, O_RDONLY) && !CHECK_FLAG(ofd->flags, O_RDWR) ){
	SET_ERRNO(EINVAL);
        return -1;
    }

    if ( (ret=fs->lowlevelfs->pread(fs->lowlevelfs, entry->inode, buf, nbytes, ofd->offset)) >= 0 ){
	/*update resulted offset*/
	int ret2 = fs->open_files_pool->set_offset(entry->open_file_description_id, ofd->offset+ret );
	assert(ret2==0);
    }
    else if ( ret < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}

static ssize_t toplevel_write(struct MountsPublicInterface* this_, int fd, const void *buf, size_t nbytes){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    CHECK_FUNC_ENSURE_EXIST(fs, pwrite);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( !CHECK_FLAG(ofd->flags, O_WRONLY) && !CHECK_FLAG(ofd->flags, O_RDWR) ){
	SET_ERRNO(EINVAL);
        return -1;
    }

    if ( (ret=fs->lowlevelfs->pwrite(fs->lowlevelfs, entry->inode, buf, nbytes, ofd->offset)) >= 0 ){
	/*update resulted offset*/
	int ret2 = fs->open_files_pool->set_offset(entry->open_file_description_id, ofd->offset+ret );
	assert(ret2==0);
    }
    else if ( ret < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}

static ssize_t 
toplevel_pread(struct MountsPublicInterface* this_, 
	       int fd, void *buf, size_t nbytes, off_t offset){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    CHECK_FUNC_ENSURE_EXIST(fs, pread);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( !CHECK_FLAG(ofd->flags, O_RDONLY) && !CHECK_FLAG(ofd->flags, O_RDWR) ){
	SET_ERRNO(EINVAL);
        return -1;
    }

    if ( (ret=fs->lowlevelfs->pread(fs->lowlevelfs, entry->inode, buf, nbytes, offset)) >= 0 ){
	/*update resulted offset*/
	int ret2 = fs->open_files_pool->set_offset(entry->open_file_description_id, offset+ret );
	assert(ret2==0);
    }
    else if ( ret < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}

static ssize_t 
toplevel_pwrite(struct MountsPublicInterface* this_, 
		int fd, const void *buf, size_t nbytes, off_t offset){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    CHECK_FUNC_ENSURE_EXIST(fs, pwrite);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( !CHECK_FLAG(ofd->flags, O_WRONLY) && !CHECK_FLAG(ofd->flags, O_RDWR) ){
	SET_ERRNO(EINVAL);
        return -1;
    }

    if ( (ret=fs->lowlevelfs->pwrite(fs->lowlevelfs, entry->inode, buf, nbytes, offset)) >= 0 ){
	/*update resulted offset*/
	int ret2 = fs->open_files_pool->set_offset(entry->open_file_description_id, offset+ret );
	assert(ret2==0);
    }
    else if ( ret < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }
    
    return ret;
}

static int toplevel_fchown(struct MountsPublicInterface* this_, int fd, uid_t owner, gid_t group){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;

    CHECK_FUNC_ENSURE_EXIST(fs, chown);
    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( (ret=fs->lowlevelfs->chown( fs->lowlevelfs, entry->inode, owner, group)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}

static int toplevel_fchmod(struct MountsPublicInterface* this_, int fd, uint32_t mode){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;

    CHECK_FUNC_ENSURE_EXIST(fs, chmod);
    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( (ret=fs->lowlevelfs->chmod( fs->lowlevelfs, entry->inode, mode)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}


static int toplevel_fstat(struct MountsPublicInterface* this_, int fd, struct stat *buf){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;

    CHECK_FUNC_ENSURE_EXIST(fs, stat);
    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( (ret=fs->lowlevelfs->stat( fs->lowlevelfs, entry->inode, buf)) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}

static int toplevel_getdents(struct MountsPublicInterface* this_, int fd, void *buf, unsigned int count){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    CHECK_FUNC_ENSURE_EXIST(fs, getdents);
    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    ssize_t readed=0;

    if ( (readed=fs->lowlevelfs
	  ->getdents(fs->lowlevelfs, entry->inode, (DIRENT*)buf, count, ofd->offset)) < 0 ){
	SET_ERRNO(-readed);
    }

    if ( (ret=readed) > 0 ){
	int ret2;
	ret2 = fs->open_files_pool->set_offset( entry->open_file_description_id, 
						ofd->offset + readed );
	assert( ret2 == 0 );
    }

    return ret;
}

static int toplevel_fsync(struct MountsPublicInterface* this_, int fd){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;

    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    errno=ENOSYS;
    return -1;
}

static int toplevel_close(struct MountsPublicInterface* this_, int fd){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    CHECK_FUNC_ENSURE_EXIST(fs, close);
    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( (ret=fs->lowlevelfs->close( fs->lowlevelfs, entry->inode, ofd->flags)) <0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    ret=fs->open_files_pool->release_ofd(entry->open_file_description_id);    
    assert( ret == 0 );
    ret = fs->handle_allocator->free_handle(fd);

    return ret;
}

static off_t toplevel_lseek(struct MountsPublicInterface* this_, int fd, off_t offset, int whence){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->open_files_pool->ofd(fd);
    struct stat st;

    CHECK_FUNC_ENSURE_EXIST(fs, stat);
    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    ret = fs->lowlevelfs->stat( fs->lowlevelfs, entry->inode, &st );
    if ( ret!=0 || !S_ISDIR(st.st_mode) ){
	SET_ERRNO(EBADF);
	return -1;
    }

    off_t next = ofd->offset;
    ssize_t len;
    switch (whence) {
    case SEEK_SET:
	next = offset;
	break;
    case SEEK_CUR:
	next += offset;
	break;
    case SEEK_END:
	len = st.st_size;
	if (len == -1) {
	    return -1;
	}
	next = static_cast<size_t>(len) + offset;
	break;
    default:
	SET_ERRNO(EINVAL);
	return -1;
    }
    // Must not seek beyond the front of the file.
    if (next < 0) {
	SET_ERRNO(EINVAL);
	return -1;
    }
    // Go to the new offset.
    ret = fs->open_files_pool->set_offset(entry->open_file_description_id, next);
    assert( ret == 0 );
    return next;
}

static int toplevel_open(struct MountsPublicInterface* this_, const char* path, int oflag, uint32_t mode){
    int ret=-1;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int parent_inode;
    struct stat st;

    CHECK_FUNC_ENSURE_EXIST(fs, open);

    GET_PARENT_ENSURE_EXIST(fs, path, &parent_inode);

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( (ret = fs->lowlevelfs->open(fs->lowlevelfs, parent_inode, name, oflag, mode)) >= 0 ){
	int inode;
	GET_INODE_ENSURE_EXIST(fs, path, &inode);
	int open_file_description_id = fs->open_files_pool->getnew_ofd(oflag);

	/*ask for file descriptor in handle allocator*/
	ret = fs->handle_allocator->allocate_handle( this_, 
						     inode,
						     open_file_description_id);
	if ( ret < 0 ){
	    /*it's hipotetical but possible case if amount of open files 
	      are exceeded an maximum value.*/
	    fs->open_files_pool->release_ofd(open_file_description_id);
	    SET_ERRNO(ENFILE);
	    return -1;
	}
	/*success*/
    }
    else if ( ret < 0 ){
	SET_ERRNO(-ret);
    }

    return ret;
}


static int toplevel_fcntl(struct MountsPublicInterface* this_, int fd, int cmd, ...){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct HandleItem* entry;

    ZRT_LOG(L_INFO, "cmd=%s", STR_FCNTL_CMD(cmd));

    SET_ERRNO(ENOSYS);
    return -1;
}

static int toplevel_remove(struct MountsPublicInterface* this_, const char* path){
    int ret=-1;
    struct stat st;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int parent_inode, inode;

    CHECK_FUNC_ENSURE_EXIST(fs, stat);
    CHECK_FUNC_ENSURE_EXIST(fs, rmdir);
    CHECK_FUNC_ENSURE_EXIST(fs, unlink);

    GET_INODE_ENSURE_EXIST(fs, path, &inode);
    GET_PARENT_ENSURE_EXIST(fs, path, &parent_inode);
    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( (ret=fs->lowlevelfs->stat( fs->lowlevelfs, inode, &st )) < 0 ){
	SET_ERRNO(ENOSYS);
	return -1;
    }

    if ( S_ISDIR(st.st_mode) ){
	if ( (ret=fs->lowlevelfs->rmdir( fs->lowlevelfs, parent_inode, name )) < 0 ){
	    SET_ERRNO(-ret);
	    return -1;
	}
    }
    else{
	if ( (ret=fs->lowlevelfs->unlink( fs->lowlevelfs, parent_inode, name )) <= 0 ){
	    SET_ERRNO(-ret);
	    return -1;
	}
    }
    return ret;
}

static int toplevel_unlink(struct MountsPublicInterface* this_, const char* path){
    int ret=-1;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int parent_inode;

    CHECK_FUNC_ENSURE_EXIST(fs, unlink);
    GET_PARENT_ENSURE_EXIST(fs, path, &parent_inode);

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( (ret=fs->lowlevelfs->unlink( fs->lowlevelfs, parent_inode, name )) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}

static int toplevel_access(struct MountsPublicInterface* this_, const char* path, int amode){
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int inode;
    int ret = -1;

    GET_PARENT_ENSURE_EXIST(fs, path, &inode);

    /*If access function not defined just pass OK result*/
    if ( fs->lowlevelfs->access ){
	if ( (ret=fs->lowlevelfs->access(fs->lowlevelfs, inode, amode)) < 0){
	    SET_ERRNO(-ret);
	    return -1;
	}
    }
    else return 0;

    return ret;
}

static int toplevel_ftruncate_size(struct MountsPublicInterface* this_, int fd, off_t length){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);
    const struct HandleItem* entry;

    CHECK_FUNC_ENSURE_EXIST(fs, ftruncate_size);
    GET_DESCRIPTOR_ENTRY_CHECK(fs, fd, entry);

    if ( is_dir(fs->lowlevelfs, entry->inode) ){
	SET_ERRNO(EISDIR);
	return -1;
    }

    int flags = ofd->flags & O_ACCMODE;
    /*check if file was not opened for writing*/
    if ( flags!=O_WRONLY && flags!=O_RDWR ){
	ZRT_LOG(L_ERROR, "file open flags=%s not allow truncate", 
		STR_FILE_OPEN_FLAGS(flags));
	SET_ERRNO( EINVAL );
	return -1;
    }

    if ( (ret=fs->lowlevelfs->ftruncate_size( fs->lowlevelfs, entry->inode, length )) >= 0 ){
	/*in according to docs: if doing file size reducing then
	  offset should not be changed, but on ubuntu linux
	  an offset can't be setted up to beyond of file bounds and
	  it assignes to max availibale pos. Do it in the same
	  manner as on host*/
#define DO_NOT_ALLOW_OFFSET_BEYOND_FILE_BOUNDS_IF_TRUNCATE_REDUCES_FILE_SIZE

#ifdef DO_NOT_ALLOW_OFFSET_BEYOND_FILE_BOUNDS_IF_TRUNCATE_REDUCES_FILE_SIZE
	off_t offset = ofd->offset;
	if ( length < offset ){
	    offset = length+1;
	    ret = fs->open_files_pool->set_offset(entry->open_file_description_id,
						  offset);
	}
#endif //DO_NOT_ALLOW_OFFSET_BEYOND_FILE_BOUNDS_IF_TRUNCATE_REDUCES_FILE_SIZE
    }
    else {
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}

int toplevel_truncate_size(struct MountsPublicInterface* this_, const char* path, off_t length){
    int ret;
    struct stat st;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int inode;

    CHECK_FUNC_ENSURE_EXIST(fs, stat);
    CHECK_FUNC_ENSURE_EXIST(fs, ftruncate_size);
    GET_INODE_ENSURE_EXIST(fs, path, &inode);

    ret=fs->lowlevelfs->stat( fs->lowlevelfs, inode, &st);
    assert(ret==0);

    if ( S_ISDIR(st.st_mode) ){
	SET_ERRNO(EISDIR);
	return -1;
    }

    if ( (ret=fs->lowlevelfs->ftruncate_size( fs->lowlevelfs, inode, length )) < 0 ){
	SET_ERRNO(-ret);
    }

    return ret;
}

static int toplevel_isatty(struct MountsPublicInterface* this_, int fd){
    return -1;
}

static int toplevel_dup(struct MountsPublicInterface* this_, int oldfd){
    return -1;
}

static int toplevel_dup2(struct MountsPublicInterface* this_, int oldfd, int newfd){
    return -1;
}

static int toplevel_link(struct MountsPublicInterface* this_, const char* oldpath, const char* newpath){
    int ret;
    struct ZfsTopLevelFs* fs = (struct ZfsTopLevelFs*)this_;
    int old_inode;
    int new_parent_inode;

    CHECK_FUNC_ENSURE_EXIST(fs, link);
    GET_INODE_ENSURE_EXIST(fs, oldpath, &old_inode);
    GET_PARENT_ENSURE_EXIST(fs, newpath, &new_parent_inode);

    const char* name = name_from_path( newpath);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( (ret=fs->lowlevelfs->link( fs->lowlevelfs, old_inode, new_parent_inode, name )) < 0 ){
	SET_ERRNO(-ret);
	return -1;
    }

    return ret;
}

static struct MountsPublicInterface KTopLevelMountWraper = {
    toplevel_readlink,
    toplevel_symlink,
    toplevel_chown,
    toplevel_chmod,
    toplevel_statvfs,
    toplevel_stat,
    toplevel_mknod,
    toplevel_mkdir,
    toplevel_rmdir,
    toplevel_read,
    toplevel_write,
    toplevel_pread,
    toplevel_pwrite,
    toplevel_fchown,
    toplevel_fchmod,
    toplevel_fstat,
    toplevel_getdents,
    toplevel_fsync,
    toplevel_close,
    toplevel_lseek,
    toplevel_open,
    //toplevel_opendir,
    toplevel_fcntl,
    toplevel_remove,
    toplevel_unlink,
    toplevel_access,
    toplevel_ftruncate_size,
    toplevel_truncate_size,
    toplevel_isatty,
    toplevel_dup,
    toplevel_dup2,
    toplevel_link,
    EMemMountId
};

struct MountsPublicInterface* 
zfs_toplevel_filesystem_construct( struct HandleAllocator* handle_allocator,
				   struct OpenFilesPool* open_files_pool,
				   struct CachedLookupPublicInterface* cached_lookup,
				   struct LowLevelFilesystemPublicInterface* lowlevelfs){
    /*use malloc and not new, because it's external c object*/
    struct ZfsTopLevelFs* this_ = (struct ZfsTopLevelFs*)malloc( sizeof(struct ZfsTopLevelFs) );

    /*set functions*/
    this_->public_ = KTopLevelMountWraper;
    /*set data members*/
    this_->handle_allocator = handle_allocator; /*use existing handle allocator*/
    this_->open_files_pool = open_files_pool; /*use existing open files pool*/
    this_->cached_lookup = cached_lookup;
    this_->lowlevelfs = lowlevelfs;
    this_->mem_mount_cpp = new MemMount;
    return (struct MountsPublicInterface*)this_;
}


