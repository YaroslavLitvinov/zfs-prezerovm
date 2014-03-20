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

#include <stddef.h> //size_t 
#include "zrt_defines.h" //INSTANCE_L

/*name of constructor*/
#define DIRENT_ENGINE get_dirent_engine

#define DIRENT struct dirent

struct DirentEnginePublicInterface{
    size_t (*adjusted_dirent_size)(int d_name_len);
    size_t (*add_dirent_into_buf)( char *buf, 
				   int buf_size, 
				   unsigned long d_ino, 
				   unsigned long d_off,
				   unsigned long mode,
				   const char *d_name );
};


struct DirentEnginePublicInterface* get_dirent_engine();
