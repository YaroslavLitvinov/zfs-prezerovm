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
#include <sys/statvfs.h>

#include "cached_lookup.h"
#include "lowlevel_filesystem.h"

struct CachedLookup{
    struct CachedLookupPublicInterface public_;
    struct LowLevelFilesystemPublicInterface* lowlevelfs;
};


static int cached_lookup_inode_by_path(struct CachedLookupPublicInterface* cached_lookup, 
				       const char *path){
    //cached_lookup->lowlevelfs->lookup();
}
static int cached_lookup_parent_inode_by_path(struct CachedLookupPublicInterface* cached_lookup, 
					      const char *path){
}
static int cached_lookup_inode_by_name(struct CachedLookupPublicInterface* cached_lookup, 
				       int parent_inode, const char *name){
}


static struct CachedLookupPublicInterface KCachedLookup = {
    cached_lookup_inode_by_path,
    cached_lookup_parent_inode_by_path,
    cached_lookup_inode_by_name
};


struct CachedLookupPublicInterface* 
cached_lookup_construct( struct LowLevelFilesystemPublicInterface* lowlevelfs ){
    /*use malloc and not new, because it's external c object*/
    struct CachedLookup* this_ = (struct CachedLookup*)malloc( sizeof(struct CachedLookup) );
    this_->public_ = KCachedLookup;
    this_->lowlevelfs = lowlevelfs;
    return (struct CachedLookupPublicInterface*)this_;
}

