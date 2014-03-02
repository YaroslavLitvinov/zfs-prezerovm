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

#ifdef __native_client__
#include "zrt_defines.h" //CONSTRUCT_L
#else
/*execute constructor function*/
#define CONSTRUCT_L(function_return_object_p) function_return_object_p
#endif 

/*name of constructor*/
#define ZFS_FILESYSTEM zfs_filesystem_construct 

struct TopLevelFilesystemObserverInterface;


/*@return result pointer can be casted to struct BitArray*/
struct LowLevelFilesystemPublicInterface* 
zfs_filesystem_construct(vfs_t *vfs,
			 struct TopLevelFilesystemObserverInterface* toplevelfs);



#endif //__ZFS_FILESYSTEM_H__
