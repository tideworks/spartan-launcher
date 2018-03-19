/* StdOutCapture.h

Copyright 2017 Tideworks Technology
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
#ifndef __SPARTAN_STDOUTCAPTURE_H__
#define __SPARTAN_STDOUTCAPTURE_H__

#include <memory>
#include <functional>
#include <unistd.h>
#include "so-export.h"
#include "spartan-exception.h"

// declare create_pipe_descriptors_exception
DECL_EXCEPTION(create_pipe_descriptors)

// declare dup_file_descriptor_exception
DECL_EXCEPTION(dup_file_descriptor)

// declare close_file_descriptor_exception
DECL_EXCEPTION(close_file_descriptor)

// declare read_file_descriptor_exception
DECL_EXCEPTION(read_file_descriptor)

using fd_cleanup_t = std::function<void (int*)>;

class SO_EXPORT StdOutCapture {
private:
  static void cleanup(int*);
  bool is_capturing = false;
  int old_stdout = -1, old_stderr = -1;
  int pipes[2] { -1, -1 };
  std::string capture_buf;
  std::unique_ptr<int, fd_cleanup_t> sp_stdout     { nullptr, cleanup };
  std::unique_ptr<int, fd_cleanup_t> sp_stderr     { nullptr, cleanup };
  std::unique_ptr<int, fd_cleanup_t> sp_read_pipe  { nullptr, cleanup };
  std::unique_ptr<int, fd_cleanup_t> sp_write_pipe { nullptr, cleanup };
public:
  StdOutCapture();
  ~StdOutCapture();
  void start_capture();
  std::string get_capture();
  void stop_capture();
  void clear() { capture_buf.clear(); }
public:
  static std::string capture_stdout_stderr(std::function<void()> action);
};

#endif //__SPARTAN_STDOUTCAPTURE_H__
