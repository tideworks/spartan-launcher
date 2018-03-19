/* fifo-pipe.h

Copyright 2015 - 2016 Tideworks Technology
Author: Roger D. Voss

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/
#ifndef __FIFO_PIPE_H__
#define __FIFO_PIPE_H__

#include "spartan-exception.h"
#include "so-export.h"

extern "C" {

// declare make_fifo_pipe_name_exception
DECL_EXCEPTION(make_fifo_pipe_name)

// declare make_fifo_pipe_exception
DECL_EXCEPTION(make_fifo_pipe)

// declare open_fifo_pipe_exception
DECL_EXCEPTION(open_fifo_pipe)

// declare close_fifo_pipe_exception
DECL_EXCEPTION(close_fifo_pipe)

SO_EXPORT int get_rnd_nbr(const unsigned int min_n, const unsigned int max_n);

// Utility function that makes a fifo pipe name
//
// throws make_fifo_pipe_name_exception on failure.
//
SO_EXPORT std::string make_fifo_pipe_name(const char * const progname, const char * const fifo_pipe_basename);

SO_EXPORT std::string make_jlauncher_fifo_pipe_name(const char * const progname);

// Utility function that makes a fifo pipe based
// on name as supplied as an argument.
//
// throws make_fifo_pipe_exception on failure.
//
SO_EXPORT void make_fifo_pipe(const char * const fifo_pipe_name);

// Utility function that opens a fifo pipe.
//
// throws open_fifo_pipe_exception on failure.
//
SO_EXPORT int open_fifo_pipe(const char * const pathname, const int flags);

// Utility function that closes a fifo pipe.
//
// throws close_fifo_pipe_exception on failure.
//
SO_EXPORT void close_fifo_pipe(const int fd, const char * const pipename);

}
#endif // __FIFO_PIPE_H__
