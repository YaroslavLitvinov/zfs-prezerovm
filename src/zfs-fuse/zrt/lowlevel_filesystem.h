/*
 * Public filesystem interface
 *
 * Copyright (c) 2012-2013, LiteStack, Inc.
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

#ifndef __LOWLEVEL_FILESYSTEM_H__
#define __LOWLEVEL_FILESYSTEM_H__

#include <stdint.h>
#include <stddef.h> //size_t
#include <unistd.h> //ssize_t

struct stat;


struct LowLevelFilesystemPublicInterface{
    int (*lookup)(struct LowLevelFilesystemPublicInterface* this_,
		  int parent_inode, const char *name);
    ssize_t (*readlink)(struct LowLevelFilesystemPublicInterface* this_,
			ino_t inode, char *buf, size_t bufsize);
    int (*symlink)(struct LowLevelFilesystemPublicInterface* this_, 
		   const char *link, ino_t parent_inode, const char *name);
    int (*chown)(struct LowLevelFilesystemPublicInterface* this_, 
		 ino_t inode, uid_t owner, gid_t group);
    int (*chmod)(struct LowLevelFilesystemPublicInterface* this_, 
		 ino_t inode, uint32_t mode);
    int (*statvfs)(struct LowLevelFilesystemPublicInterface* this_, 
		   struct statvfs *buf);
    int (*stat)(struct LowLevelFilesystemPublicInterface* this_, 
		ino_t inode, struct stat *buf);
    int (*access)(struct LowLevelFilesystemPublicInterface* this_, 
		ino_t inode, int mode);
    int (*mknod)(struct LowLevelFilesystemPublicInterface* this_,
		 ino_t parent_inode, const char *name, mode_t mode, dev_t rdev);
    int (*mkdir)(struct LowLevelFilesystemPublicInterface* this_, 
		 ino_t parent_inode, const char* name, uint32_t mode);
    int (*rmdir)(struct LowLevelFilesystemPublicInterface* this_, 
		 ino_t parent_inode, const char* name);
    ssize_t (*pread)(struct LowLevelFilesystemPublicInterface* this_,
		     ino_t inode, void *buf, size_t nbyte, off_t offset);
    ssize_t (*pwrite)(struct LowLevelFilesystemPublicInterface* this_,
		      ino_t inode, const void *buf, size_t nbyte, off_t offset);
    int (*getdents)(struct LowLevelFilesystemPublicInterface* this_, 
		    ino_t inode, void *buf, unsigned int count, off_t offset,
		    int *lastcall_workaround);
    int (*fsync)(struct LowLevelFilesystemPublicInterface* this_, 
		 ino_t inode);
    int (*close)(struct LowLevelFilesystemPublicInterface* this_, ino_t inode, int flags);
    int (*open)(struct LowLevelFilesystemPublicInterface* this_, 
		ino_t parent_inode, const char* name, int oflag, uint32_t mode);
    //int (*opendir)(struct LowLevelFilesystemPublicInterface* this_, ino_t inode);
    int (*unlink)(struct LowLevelFilesystemPublicInterface* this_, 
		  ino_t parent_inode, const char* name);
    int (*link)(struct LowLevelFilesystemPublicInterface* this_, 
		ino_t inode, ino_t new_parent, const char *newname);
#ifndef __native_client__
    int (*rename)(struct LowLevelFilesystemPublicInterface* this_,
		      ino_t parent, const char *name,
		      ino_t new_parent, const char *newname);
#endif //__native_client__
    int (*ftruncate_size)(struct LowLevelFilesystemPublicInterface* this_, 
			  ino_t inode, off_t length);
    struct DirentEnginePublicInterface* dirent_engine;
};


#endif //__LOWLEVEL_FILESYSTEM_H__
