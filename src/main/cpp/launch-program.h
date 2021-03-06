/* launch-program.h

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
#ifndef __LAUNCH_PROGRAM_H__
#define __LAUNCH_PROGRAM_H__

#include "string-view.h"
#include <functional>
#include <string>
#include <memory>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>

namespace launch_program {

  using bpstd::string_view;

  std::tuple<std::string, bool> try_resolve_program_path(const char * const prog, const char * const path_var_name);

  // RAII-related declarations for managing file/pipe descriptors (to clean these up if exception thrown)
  struct fd_wrapper_t {
    pid_t const pid;
    int fd;
    std::string const name;
    explicit fd_wrapper_t(int fd) : pid{0}, fd{fd}, name{} {}
    explicit fd_wrapper_t(int fd, const char * const name) : pid{ ::getpid() }, fd{fd}, name{name} {}
  };
  using fd_wrapper_cleanup_t = void(*)(fd_wrapper_t *);
  using fd_wrapper_sp_t = std::unique_ptr<fd_wrapper_t, fd_wrapper_cleanup_t>;
  void fd_cleanup_no_delete(fd_wrapper_t *);
  void fd_cleanup_with_delete(fd_wrapper_t *);

  struct pid_buffer_t {
    pid_t pid;
    int fd_rtn_count;
  };

  union pipe_fds_buffer_t {
    cmsghdr cmsg;
    struct {
      unsigned char cmsg_offset[sizeof(cmsg)];
      int pipe_fds[1];
    } p;
  };

  union pipes_fds_buffer_t {
    cmsghdr cmsg;
    struct {
      unsigned char cmsg_offset[sizeof(cmsg)];
      int pipe_fds[3];
    } p;
  };

  void init_sockaddr(string_view const uds_sock_name, sockaddr_un &addr, socklen_t &addr_len);
  fd_wrapper_sp_t create_uds_socket(std::function<std::string(int)> get_errmsg);
  std::tuple<fd_wrapper_sp_t, std::string> bind_uds_socket_name(const char* const sub_cmd);
  std::tuple<pid_t, fd_wrapper_sp_t, fd_wrapper_sp_t, fd_wrapper_sp_t> obtain_response_stream(
      string_view const uds_socket_name, fd_wrapper_sp_t socket_read_fd_sp);
}

#endif //__LAUNCH_PROGRAM_H__