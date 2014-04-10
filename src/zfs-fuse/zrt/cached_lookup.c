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
#include <string.h>
#include <alloca.h>

#include "path_utils.h"
#include "lowlevel_filesystem.h"
#include "cached_lookup.h"


struct CachedLookup{
    struct CachedLookupPublicInterface public_;
    struct LowLevelFilesystemPublicInterface* lowlevelfs;
};


static int cached_lookup_inode_by_path(struct CachedLookupPublicInterface* cached_lookup, 
				       const char *path){
    int component_len;
    const char *component; 
    int temp_cursor;
    int inode=-1;
    int proceed=1;
    INIT_TEMP_CURSOR(&temp_cursor);
    while ( proceed && 
	   (component = path_component_forward( &temp_cursor, path, &component_len)) != NULL ){
	/*root inode=3*/
	if ( component_len ==1 && component[0] == '/' ){
	    inode = 3;
	}
	else{
	    assert(inode>=3);
	    inode = cached_lookup->inode_by_name(cached_lookup, 
						 inode, 
						 strndupa( component, component_len ) );
	    
	}
    }
    return inode;
}

static int cached_lookup_parent_inode_by_path(struct CachedLookupPublicInterface* cached_lookup, 
					      const char *path){
    int temp_cursor;
    int result_len;
    const char *res;
    INIT_TEMP_CURSOR(&temp_cursor);
    res = path_subpath_backward(&temp_cursor, path, &result_len); /*rewind to last component*/
    /*if path not root*/
    if ( strcmp("/", path) ){
	res = path_subpath_backward(&temp_cursor, path, &result_len); /*rewind to pre-last component*/
    }

    /*if no parent - root path was passed*/
    if ( res == NULL )
	return -1;
    else{
	return cached_lookup->inode_by_path(cached_lookup, 
					    strndupa( res, result_len ) );
    }
}

static int cached_lookup_inode_by_name(struct CachedLookupPublicInterface* cached_lookup, 
				       int parent_inode, const char *name){
    struct CachedLookup* cached_lookup_impl = (struct CachedLookup*)cached_lookup;
    return cached_lookup_impl->lowlevelfs->
	lookup(cached_lookup_impl->lowlevelfs, parent_inode, name);
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
    test_path_utils();
    return (struct CachedLookupPublicInterface*)this_;
}

