/*
 * ZFS filesystem interface implementation
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


#ifndef __ZFS_FILESYSTEM_H__
#define __ZFS_FILESYSTEM_H__

#include <sys/vfs.h>

#include "zrt_defines.h" //CONSTRUCT_L

/*name of constructor*/
#define ZFS_FILESYSTEM zfs_filesystem_construct 

struct DirentEnginePublicInterface;

struct LowLevelFilesystemPublicInterface* 
zfs_filesystem_construct(vfs_t *vfs,
			 struct DirentEnginePublicInterface* dirent_engine);



#endif //__ZFS_FILESYSTEM_H__
