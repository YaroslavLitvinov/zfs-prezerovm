/*
 *
 * Copyright (c) 2012-2013, LiteStack, Inc.
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


#ifndef __ZFS_TOPLEVEL_FILESYSTEM_H__
#define __ZFS_TOPLEVEL_FILESYSTEM_H__

#include "mounts_interface.h" //struct MountsPublicInterface

#include "zrt_defines.h" //CONSTRUCT_L

/*name of constructor*/
#define ZFS_TOPLEVEL_FILESYSTEM zfs_toplevel_filesystem_construct

struct CachedLookupPublicInterface;
struct HandleAllocator;
struct OpenFilesPool;

struct MountsPublicInterface* 
zfs_toplevel_filesystem_construct( struct HandleAllocator* handle_allocator,
                                   struct OpenFilesPool* open_files_pool,
                                   struct CachedLookupPublicInterface* cached_lookup,
                                   struct LowLevelFilesystemPublicInterface* lowlevelfs);
    


#endif /* __ZFS_TOPLEVEL_FILESYSTEM_H__ */
