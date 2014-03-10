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
#include "toplevel_filesystem.h"
#include "mounts_interface.h"

extern "C" {
#include "handle_allocator.h" //struct HandleAllocator, struct HandleItem
#include "open_file_description.h" //struct OpenFilesPool, struct OpenFileDescription
#include "dirent_engine.h"
    //#include "fstab_observer.h" /*lazy mount*/
    //#include "fcntl_implem.h"
    //#include "enum_strings.h"
}

#define GET_DESCRIPTOR_ENTRY_CHECK(fs, entry_p)		\
    entry_p = fs->handle_allocator->entry(fd);		\
    if ( entry == NULL ){				\
	SET_ERRNO(EBADF);				\
	return -1;					\
    }

#define NAME_LENGTH_CHECK(name)			\
    if ( strlen(name) > NAME_MAX ){		\
	SET_ERRNO(ENAMETOOLONG);		\
        return -1;				\
    }

struct TopLevelFs{
    struct MountsPublicInterface public_;
    struct HandleAllocator* handle_allocator;
    struct OpenFilesPool*   open_files_pool;
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

/*wraper implementation*/

static ssize_t toplevel_readlink(struct MountsPublicInterface* this_,
				 const char *path, char *buf, size_t bufsize){
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *node;
    int ret = -1;

    NAME_LENGTH_CHECK(path);

    node = fs->mem_mount_cpp->GetMemNode(path);

    if ( !node ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    if ( fs->lowlevelfs->readlink
	 && (ret=fs->lowlevelfs->readlink(fs->lowlevelfs, node->slot(), buf, bufsize)) != -1 ){
	;
    }
    
    return ret;
}

static int toplevel_symlink(struct MountsPublicInterface* this_,
			    const char *oldpath, const char *newpath){
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *newnode_parent= fs->mem_mount_cpp->GetParentMemNode(newpath);
    MemNode *newnode = fs->mem_mount_cpp->GetMemNode(newpath);

    if (oldpath == NULL && !strlen(oldpath)) {
	SET_ERRNO(ENOENT);
        return -1;
    }
    if (newnode != NULL) {
	SET_ERRNO(EEXIST);
        return -1;
    }

    const char* name = name_from_path( newpath);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }
    if ( fs->lowlevelfs->symlink &&
	 (ret=fs->lowlevelfs->symlink( fs->lowlevelfs, oldpath, newnode_parent->slot(), name )) == 0 ){
	;
    }
    return ret;
}



static int toplevel_chown(struct MountsPublicInterface* this_, const char* path, uid_t owner, gid_t group){
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *node;
    int ret = -1;

    NAME_LENGTH_CHECK(path);

    node = fs->mem_mount_cpp->GetMemNode(path);

    if ( !node ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    if ( fs->lowlevelfs->chown
	 && (ret=fs->lowlevelfs->chown(fs->lowlevelfs, node->slot(), owner, group)) != -1 ){
	;
    }
    
    return ret;
}

static int toplevel_chmod(struct MountsPublicInterface* this_, const char* path, uint32_t mode){
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *node;
    int ret = -1;

    NAME_LENGTH_CHECK(path);

    node = fs->mem_mount_cpp->GetMemNode(path);

    if ( !node ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    if ( fs->lowlevelfs->chmod
	 && (ret=fs->lowlevelfs->chmod(fs->lowlevelfs, node->slot(), mode)) != -1 ){
	;
    }
    
    return ret;
}

static int toplevel_statvfs(struct MountsPublicInterface* this_, const char* path, struct statvfs *buf){
    /*currently path is not used, but it will be used to select
     appropriate to path a lowlevel fs and to get this call for
     particular fs.*/

    struct TopLevelFs* fs = (struct TopLevelFs*)this_;

    if ( fs->lowlevelfs->statvfs
	 && (ret=fs->lowlevelfs->statvfs(fs->lowlevelfs, buf)) != -1){
	;
    }
    
    return ret;
}


static int toplevel_stat(struct MountsPublicInterface* this_, const char* path, struct stat *buf){
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *node;
    int ret = -1;

    NAME_LENGTH_CHECK(path);

    node = fs->mem_mount_cpp->GetMemNode(path);

    if ( !node ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    if ( fs->lowlevelfs->stat
	 && (ret=fs->lowlevelfs->stat(fs->lowlevelfs, node->slot(), buf)) != -1
	 && node->hardinode() > 0 ){
	/*fixes for hardlinks pseudo support, different hardlinks must have same inode,
	 *but internally all nodes have separeted inodes*/
	/*patch inode if it has hardlinks*/
	buf->st_ino = (ino_t)node->hardinode();
    }
    
    return ret;
}

static int toplevel_mknod(struct MountsPublicInterface* this_, 
			  const char* path, mode_t mode, dev_t dev){
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *node;
    int ret = -1;

    NAME_LENGTH_CHECK(path);

    node = fs->mem_mount_cpp->GetMemNode(path);

    if ( !node ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    if ( fs->lowlevelfs->mknod
	 && (ret=fs->lowlevelfs->mknod(fs->lowlevelfs, node->slot(), mode, dev)) != -1 ){
	;
    }
    
    return ret;
}

static int toplevel_mkdir(struct MountsPublicInterface* this_, const char* path, uint32_t mode){
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *parent_node;
    int ret=-1;

    NAME_LENGTH_CHECK(path);

    if ( fs->mem_mount_cpp->GetMemNode(path) != NULL ){
	SET_ERRNO(EEXIST);
	return -1;
    }

    if ( (parent_node=fs->mem_mount_cpp->GetParentMemNode(path)) == NULL ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if (fs->lowlevelfs->mkdir != NULL ){
	ret = fs->lowlevelfs->mkdir(fs->lowlevelfs, parent_node->slot(), name, mode);
    }
    return ret;
}


static int toplevel_rmdir(struct MountsPublicInterface* this_, const char* path){
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *parent_node;
    int ret;

    NAME_LENGTH_CHECK(path);

    if ( fs->mem_mount_cpp->GetMemNode(path) != NULL ){
	SET_ERRNO(EEXIST);
	return -1;
    }

    if ( (parent_node=fs->mem_mount_cpp->GetParentMemNode(path)) == NULL ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if (fs->lowlevelfs->rmdir != NULL ){
	ret = fs->lowlevelfs->rmdir(fs->lowlevelfs, parent_node->slot(), name);
    }
    return ret;
}

static ssize_t toplevel_read(struct MountsPublicInterface* this_, int fd, void *buf, size_t nbytes){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( !CHECK_FLAG(ofd->flags, O_RDONLY) && !CHECK_FLAG(ofd->flags, O_RDWR) ){
	SET_ERRNO(EINVAL);
        return -1;
    }

    if ( fs->lowlevelfs->pread &&
	(ret=fs->lowlevelfs->pread(fs->lowlevelfs, entry->inode, buf, nbytes, ofd->offset)) != -1 ){
	/*update resulted offset*/
	int ret2 = fs->open_files_pool->set_offset(entry->open_file_description_id, ofd->offset+ret );
	assert(ret2==0);
    }
    
    return ret;
}

static ssize_t toplevel_write(struct MountsPublicInterface* this_, int fd, const void *buf, size_t nbytes){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( !CHECK_FLAG(ofd->flags, O_WRONLY) && !CHECK_FLAG(ofd->flags, O_RDWR) ){
	SET_ERRNO(EINVAL);
        return -1;
    }

    if ( fs->lowlevelfs->pwrite &&
	 (ret=fs->lowlevelfs->pwrite(fs->lowlevelfs, entry->inode, buf, nbytes, ofd->offset)) != -1 ){
	/*update resulted offset*/
	int ret2 = fs->open_files_pool->set_offset(entry->open_file_description_id, ofd->offset+ret );
	assert(ret2==0);
    }
    
    return ret;
}

static ssize_t toplevel_pread(struct MountsPublicInterface* this_, 
			 int fd, void *buf, size_t nbytes, off_t offset){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( !CHECK_FLAG(ofd->flags, O_RDONLY) && !CHECK_FLAG(ofd->flags, O_RDWR) ){
	SET_ERRNO(EINVAL);
        return -1;
    }

    if ( fs->lowlevelfs->pread &&
	 (ret=fs->lowlevelfs->pread(fs->lowlevelfs, entry->inode, buf, nbytes, offset)) != -1 ){
	/*update resulted offset*/
	int ret2 = fs->open_files_pool->set_offset(entry->open_file_description_id, offset+ret );
	assert(ret2==0);
    }
    
    return ret;
}

static ssize_t toplevel_pwrite(struct MountsPublicInterface* this_, 
			  int fd, const void *buf, size_t nbytes, off_t offset){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( !CHECK_FLAG(ofd->flags, O_WRONLY) && !CHECK_FLAG(ofd->flags, O_RDWR) ){
	SET_ERRNO(EINVAL);
        return -1;
    }

    if ( fs->lowlevelfs->pwrite &&
	 (ret=fs->lowlevelfs->pwrite(fs->lowlevelfs, entry->inode, buf, nbytes, offset)) != -1 ){
	/*update resulted offset*/
	int ret2 = fs->open_files_pool->set_offset(entry->open_file_description_id, offset+ret );
	assert(ret2==0);
    }
    
    return ret;
}

static int toplevel_fchown(struct MountsPublicInterface* this_, int fd, uid_t owner, gid_t group){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( fs->lowlevelfs->chown &&
	 (ret=fs->lowlevelfs->chown( fs->lowlevelfs, entry->inode, owner, group)) != -1 ){
	;
    }
    return ret;
}

static int toplevel_fchmod(struct MountsPublicInterface* this_, int fd, uint32_t mode){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( fs->lowlevelfs->chmod &&
	 (ret=fs->lowlevelfs->chmod( fs->lowlevelfs, entry->inode, mode)) != -1 ){
	;
    }
    return ret;
}


static int toplevel_fstat(struct MountsPublicInterface* this_, int fd, struct stat *buf){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( fs->lowlevelfs->stat &&
	 (ret=fs->lowlevelfs->stat( fs->lowlevelfs, entry->inode, buf)) != -1 ){
	;
    }
    return ret;
}

static int toplevel_getdents(struct MountsPublicInterface* this_, int fd, void *buf, unsigned int count){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    ssize_t readed=0;

    if ( fs->lowlevelfs->getdents &&
	 (readed=fs->lowlevelfs
	  ->getdents(fs->lowlevelfs, entry->inode, (DIRENT*)buf, count, ofd->offset)) != -1 ){
    }
    else if ( (readed=fs->mem_mount_cpp->Getdents(entry->inode, ofd->offset, (DIRENT*)buf, count)) != -1 ){
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
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    errno=ENOSYS;
    return -1;
}

static int toplevel_close(struct MountsPublicInterface* this_, int fd){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( fs->lowlevelfs->close &&
	 (ret=fs->lowlevelfs->close( fs->lowlevelfs, entry->inode, ofd->flags)) != -1 ){
    }

    fs->mem_mount_cpp->Unref(entry->inode); /*decrement use count*/

    struct MemNode* node = fs->mem_mount_cpp->ToMemNode(entry->inode);
    assert(node!=NULL);
    if ( node->UnlinkisTrying() ){
        ret = fs->mem_mount_cpp->UnlinkInternal(node);
        assert( ret == 0 );	
    }

    ret=fs->open_files_pool->release_ofd(entry->open_file_description_id);    
    assert( ret == 0 );
    ret = fs->handle_allocator->free_handle(fd);

    return ret;
}

static off_t toplevel_lseek(struct MountsPublicInterface* this_, int fd, off_t offset, int whence){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;
    const struct OpenFileDescription* ofd = fs->open_files_pool->ofd(fd);
    struct stat st;

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

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
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *parent_node;
    struct stat st;
    MemNode *node = fs->mem_mount_cpp->GetMemNode(path);

    NAME_LENGTH_CHECK(path);

    if ( oflag&O_CREAT && oflag&O_EXCL 
	 && node != NULL ){
	SET_ERRNO(EEXIST);
	return -1;
    }

    if ( fs->lowlevelfs->stat ){
	ret=fs->lowlevelfs->stat( fs->lowlevelfs, node->slot(), &st );
    }
    else{
	SET_ERRNO(ENOSYS);
	return -1;
    }

    if ( oflag&O_DIRECTORY ){
	int ret;
	if ( !S_ISDIR(st.st_mode) ){
	    SET_ERRNO(ENOTDIR);
	    return -1;
	}
    }

    if ( (parent_node=fs->mem_mount_cpp->GetParentMemNode(path)) == NULL ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if (fs->lowlevelfs->open != NULL &&
	(ret = fs->lowlevelfs->open(fs->lowlevelfs, parent_node->slot(), name, oflag, mode)) != -1 ){

	int open_file_description_id = fs->open_files_pool->getnew_ofd(oflag);

	/*ask for file descriptor in handle allocator*/
	ret = fs->handle_allocator->allocate_handle( this_, 
							node->slot(),
							open_file_description_id);
	if ( ret < 0 ){
	    /*it's hipotetical but possible case if amount of open files 
	      are exceeded an maximum value.*/
	    fs->open_files_pool->release_ofd(open_file_description_id);
	    SET_ERRNO(ENFILE);
	    return -1;
	}
	fs->mem_mount_cpp->Ref(node->slot()); /*set file referred*/
	/*success*/
    }
    return ret;
}

//open
	// /*append feature support, is simple*/
	// if ( oflag & O_APPEND ){
	//     ZRT_LOG(L_SHORT, P_TEXT, "handle flag: O_APPEND");
	//     mem_lseek(this_, fd, 0, SEEK_END);
	// }
	// /*file truncate support, only for writable files, reset size*/
	// if ( oflag&O_TRUNC && (oflag&O_RDWR || oflag&O_WRONLY) ){
	//     /*reset file size*/
	//     MemNode* mnode = NODE_OBJECT_BYINODE( MEMOUNT_BY_MOUNT(this_), st.st_ino);
	//     if (mnode){ 
	// 	ZRT_LOG(L_SHORT, P_TEXT, "handle flag: O_TRUNC");
	// 	/*update stat*/
	// 	st.st_size = 0;
	// 	mnode->set_len(st.st_size);
	// 	ZRT_LOG(L_SHORT, "%s, %d", mnode->name().c_str(), mnode->len() );
	//     }
	// }


static int toplevel_fcntl(struct MountsPublicInterface* this_, int fd, int cmd, ...){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct HandleItem* entry;

    ZRT_LOG(L_INFO, "cmd=%s", STR_FCNTL_CMD(cmd));
    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

    if ( is_dir(fs->lowlevelfs, entry->inode) ){
	SET_ERRNO(EBADF);
	return -1;
    }

    SET_ERRNO(ENOSYS);
    return -1;
}

static int toplevel_remove(struct MountsPublicInterface* this_, const char* path){
    int ret=-1;
    struct stat st;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *parent_node = fs->mem_mount_cpp->GetParentMemNode(path);
    MemNode *node = fs->mem_mount_cpp->GetMemNode(path);

    NAME_LENGTH_CHECK(path);

    if ( node==NULL || parent_node ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( fs->lowlevelfs->stat ){
	ret=fs->lowlevelfs->stat( fs->lowlevelfs, node->slot(), &st );
    }
    else{
	SET_ERRNO(ENOSYS);
	return -1;
    }

    if ( S_ISDIR(st.st_mode) ){
	if ( fs->lowlevelfs->rmdir &&
	     (ret=fs->lowlevelfs->rmdir( fs->lowlevelfs, parent_node->slot(), name )) == 0 ){
	    ;
	}
    }
    else{
	if ( fs->lowlevelfs->unlink &&
	     (ret=fs->lowlevelfs->unlink( fs->lowlevelfs, parent_node->slot(), name )) == 0 ){
	    ;
	}
    }
    return ret;
}

static int toplevel_unlink(struct MountsPublicInterface* this_, const char* path){
    int ret=-1;
    struct stat st;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *parent_node = fs->mem_mount_cpp->GetParentMemNode(path);
    MemNode *node = fs->mem_mount_cpp->GetMemNode(path);

    NAME_LENGTH_CHECK(path);

    if ( node==NULL || parent_node ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    const char* name = name_from_path( path);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( fs->lowlevelfs->stat ){
	ret=fs->lowlevelfs->stat( fs->lowlevelfs, node->slot(), &st );
    }
    else{
	SET_ERRNO(ENOSYS);
	return -1;
    }

   if ( S_ISDIR(st.st_mode) ){
	SET_ERRNO(EISDIR);
	return -1;
    }

   if ( fs->lowlevelfs->unlink &&
	(ret=fs->lowlevelfs->unlink( fs->lowlevelfs, parent_node->slot(), name )) == 0 ){
       fs->mem_mount_cpp->UnlinkInternal(node);
   }
   return ret;
}

static int toplevel_access(struct MountsPublicInterface* this_, const char* path, int amode){
    SET_ERRNO(ENOSYS);
    return -1;
}

static int toplevel_ftruncate_size(struct MountsPublicInterface* this_, int fd, off_t length){
    int ret;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    const struct OpenFileDescription* ofd = fs->handle_allocator->ofd(fd);
    const struct HandleItem* entry;

    GET_DESCRIPTOR_ENTRY_CHECK(fs, entry);

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

   if ( fs->lowlevelfs->ftruncate_size &&
	(ret=fs->lowlevelfs->ftruncate_size( fs->lowlevelfs, entry->inode, length )) == 0 ){
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
   return ret;
}

int toplevel_truncate_size(struct MountsPublicInterface* this_, const char* path, off_t length){
    int ret;
    struct stat st;
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *node = fs->mem_mount_cpp->GetMemNode(path);

    if ( node==NULL ){
	SET_ERRNO(ENOENT);
	return -1;
    }

    if ( fs->lowlevelfs->stat ){
	ret=fs->lowlevelfs->stat( fs->lowlevelfs, node->slot(), &st);
    }
    else{
	ZRT_LOG(L_ERROR, P_TEXT, "lowlevelfs stat not defined");
	SET_ERRNO(ENOSYS);
	return -1;
    }

    if ( S_ISDIR(st.st_mode) ){
	SET_ERRNO(EISDIR);
	return -1;
    }

    if ( fs->lowlevelfs->ftruncate_size &&
	 (ret=fs->lowlevelfs->ftruncate_size( fs->lowlevelfs, node->slot(), length )) == 0 ){
	;
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
    struct TopLevelFs* fs = (struct TopLevelFs*)this_;
    MemNode *oldnode_parent= fs->mem_mount_cpp->GetParentMemNode(oldpath);
    MemNode *oldnode = fs->mem_mount_cpp->GetMemNode(oldpath);
    MemNode *newnode = fs->mem_mount_cpp->GetMemNode(newpath);

    if (oldnode == NULL) {
	SET_ERRNO(ENOENT);
        return -1;
    }
    // Check that it's not the root.
    if (oldnode_parent == NULL) {
	SET_ERRNO(EBUSY);
        return -1;
    }
    if (newnode != NULL) {
	SET_ERRNO(EEXIST);
        return -1;
    }

    const char* name = name_from_path( newpath);
    if ( name == NULL ){
	SET_ERRNO(ENOTDIR);
	return -1;
    }

    if ( fs->lowlevelfs->link &&
	 (ret=fs->lowlevelfs->link( fs->lowlevelfs, oldnode->slot(), newnode->slot(), name )) == 0 ){
	;
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
toplevel_filesystem_construct( struct HandleAllocator* handle_allocator,
			       struct OpenFilesPool* open_files_pool,
			       struct LowLevelFilesystemPublicInterface* lowlevelfs){
    /*use malloc and not new, because it's external c object*/
    struct TopLevelFs* this_ = (struct TopLevelFs*)malloc( sizeof(struct TopLevelFs) );

    /*set functions*/
    this_->public_ = KTopLevelMountWraper;
    /*set data members*/
    this_->handle_allocator = handle_allocator; /*use existing handle allocator*/
    this_->open_files_pool = open_files_pool; /*use existing open files pool*/
    this_->lowlevelfs = lowlevelfs;
    this_->mem_mount_cpp = new MemMount;
    return (struct MountsPublicInterface*)this_;
}


