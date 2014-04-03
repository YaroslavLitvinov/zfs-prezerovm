/*
 * ZFS mounts interface implementation
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

#include "zfs_filesystem.h"
#include "zfs_toplevel_filesystem.h"
#include "open_file_description.h"
#include "lowlevel_filesystem.h"
#include "handle_allocator.h"
#include "dirent_engine.h"
#include "cached_lookup.h"

extern vfs_t*  s_vfs;

struct MountsPublicInterface* zfs_mounts_construct(vfs_t *vfs){
    assert(vfs);
    struct DirentEnginePublicInterface* dirent_engine = 
	INSTANCE_L(DIRENT_ENGINE)();

    /*create filesystem that driven by inodes, this fs is used by toplevelfs,
     and only toplevelfs must provide inodes*/
    struct LowLevelFilesystemPublicInterface* zfs_lowlevel_fs = 
	CONSTRUCT_L(ZFS_FILESYSTEM)( s_vfs, dirent_engine );

    struct CachedLookupPublicInterface* zfs_cached_lookup =
	CONSTRUCT_L(CACHED_LOOKUP)( zfs_lowlevel_fs );

    /*create filesystem implementation of much top level, which can
     accept paths, this interface purely can be used inside of ZRT*/
    struct MountsPublicInterface* toplevel_fs = 
	CONSTRUCT_L(ZFS_TOPLEVEL_FILESYSTEM)( INSTANCE_L(HANDLE_ALLOCATOR)(),
					      INSTANCE_L(OPEN_FILES_POOL)(),
					      zfs_cached_lookup,
					      zfs_lowlevel_fs);
    return toplevel_fs;
}

