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

#include <string>
#include <sys/un.h>
#include <sys/socket.h>
#include "so-export.h"

namespace launch_program {
  // NOTE: this property must be set prior to using Java_spartan_LaunchProgram_invokeCommand()
  SO_EXPORT void set_progpath(const char *const progpath);
  SO_EXPORT std::tuple<std::string, bool> try_resolve_program_path(const char * const prog, const char * const path_var_name);

  // RAII-related declarations for managing file/pipe descriptors (to clean these up if exception thrown)
  struct fd_wrapper_t {
    pid_t const pid;
    int fd;
    std::string const name;
    explicit fd_wrapper_t(int fd) : pid{0}, fd{fd}, name{} {}
    explicit fd_wrapper_t(int fd, const char * const name) : pid{ getpid() }, fd{fd}, name{name} {}
  };
  using fd_wrapper_cleanup_t = void(*)(fd_wrapper_t *);
  using fd_wrapper_sp_t = std::unique_ptr<fd_wrapper_t, fd_wrapper_cleanup_t>;
  SO_EXPORT void fd_cleanup_no_delete(fd_wrapper_t *);
  SO_EXPORT void fd_cleanup_with_delete(fd_wrapper_t *);

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

  SO_EXPORT void init_sockaddr(std::string const &uds_sock_name, sockaddr_un &addr, socklen_t &addr_len);
  SO_EXPORT fd_wrapper_sp_t create_uds_socket(std::function<std::string(int)> get_errmsg);
  SO_EXPORT std::tuple<fd_wrapper_sp_t, std::string> bind_uds_socket_name(const char* const sub_cmd);
  SO_EXPORT std::tuple<pid_t, fd_wrapper_sp_t> obtain_response_stream(std::string const &uds_socket_name,
                                                                      fd_wrapper_sp_t socket_read_fd_sp);
}

#endif //__LAUNCH_PROGRAM_H__