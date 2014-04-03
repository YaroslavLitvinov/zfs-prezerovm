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

#include "zrt_defines.h" //CONSTRUCT_L

/*name of constructor*/
#define CACHED_LOOKUP cached_lookup_construct

struct LowLevelFilesystemPublicInterface* lowlevelfs;

struct CachedLookupPublicInterface{
    /*get inode, -1 not located*/
    int (*inode_by_path)(struct CachedLookupPublicInterface* cached_lookup, 
			 const char *path);
    int (*parent_inode_by_path)(struct CachedLookupPublicInterface* cached_lookup, 
				const char *path);
    int (*inode_by_name)(struct CachedLookupPublicInterface* cached_lookup, 
			 int parent_inode, const char *name);
};


struct CachedLookupPublicInterface* 
cached_lookup_construct (struct LowLevelFilesystemPublicInterface* lowlevelfs);
