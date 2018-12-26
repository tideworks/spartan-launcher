/* open-anon-pipes.cpp

Copyright 2018 Tideworks Technology
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
#include <memory>
#include <unistd.h>
#include "format2str.h"
#include "spartan-exception.h"
#include "log.h"
#include "open-anon-pipes.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using logger::LL;

using namespace launch_program;

using namespace bpstd;

DECL_EXCEPTION(open_write_pipe)

enum PIPES : short { READ = 0, WRITE = 1 };

static std::tuple<int, int> make_anon_pipe() {
  int pipes[2] { -1, -1 };
  int line_nbr = __LINE__ + 1;
  auto rtn = pipe(pipes);
  if (rtn == -1) {
    const char err_msg_fmt[] = "%d: %s() -> pipe(): failed creating pipe file descriptor pair:\n\t%s";
    throw open_write_pipe_exception{ format2str(err_msg_fmt, line_nbr, __FUNCTION__, strerror(errno)) };
  }
  return std::make_tuple(pipes[PIPES::READ], pipes[PIPES::WRITE]);
}

static void send_pid_and_fd_count(string_view const uds_socket_name, sockaddr_un &server_address, const int socket_fd,
                                 const pid_t pid, const int fds_count)
{
  static const char* const func_name = __FUNCTION__;
  socklen_t address_length;
  init_sockaddr(uds_socket_name, server_address, address_length);

  pid_buffer_t pid_buffer{ pid, fds_count };

  int line_nbr;
  auto bytes_sent = sendto(socket_fd,
                           &pid_buffer,
                           sizeof(pid_buffer),
                           0,
                           (sockaddr*) &server_address,
                           address_length); line_nbr = __LINE__;
  if (bytes_sent < 0) {
    const char err_fmt[] = "%d: %s() -> sendto(): failed sending process pid{%d} datagram via named socket %s:\n\t%s";
    auto err_msg = format2str(err_fmt, line_nbr, func_name, pid_buffer.pid, uds_socket_name.c_str(), strerror(errno));
    throw open_write_pipe_exception{ std::move(err_msg) };
  }
  assert(bytes_sent == (long) sizeof(pid_buffer));

  log(LL::DEBUG, "%s(): ***** sent process pid{%d} datagram via named socket %s *****\n",
      func_name, pid_buffer.pid, uds_socket_name.c_str());
}

fd_wrapper_sp_t open_write_anon_pipe(string_view const uds_socket_name, int &rc) {
  static const char* const func_name = __FUNCTION__;
  rc = EXIT_SUCCESS;

  int line_nbr = __LINE__ + 1;
  auto socket_fd_sp = create_uds_socket([uds_socket_name, &line_nbr](int err_no) -> std::string {
    const char err_msg_fmt[] = "%d: %s() -> create_uds_socket(): failed creating uds socket for use with name %s:\n\t%s";
    return format2str(err_msg_fmt, line_nbr, func_name, uds_socket_name.c_str(), strerror(err_no));
  });

  const auto pipe_fds = make_anon_pipe();

  fd_wrapper_t rdr_rd_pipe{ std::get<PIPES::READ>(pipe_fds) };
  fd_wrapper_sp_t rdr_rd_pipe_sp{ &rdr_rd_pipe, &fd_cleanup_no_delete };
  fd_wrapper_sp_t rdr_wr_pipe_sp{ new fd_wrapper_t{ std::get<PIPES::WRITE>(pipe_fds), uds_socket_name.c_str() },
                                  &fd_cleanup_with_delete };

  sockaddr_un server_address{0};
  socklen_t address_length;

  send_pid_and_fd_count(uds_socket_name, server_address, socket_fd_sp->fd, rdr_wr_pipe_sp->pid, 1);

  init_sockaddr(uds_socket_name, server_address, address_length);

  msghdr parent_msg{nullptr};
  memset(&parent_msg, 0, sizeof(parent_msg));
  parent_msg.msg_name = &server_address;
  parent_msg.msg_namelen = address_length;

  pipe_fds_buffer_t cmsg_payload{}; //{ { 0, SOL_SOCKET, SCM_RIGHTS }, { rdr_rd_pipe_sp->fd } };
  memset(&cmsg_payload, 0, sizeof(cmsg_payload));

  cmsg_payload.cmsg.cmsg_len = 0;
  cmsg_payload.cmsg.cmsg_level = SOL_SOCKET;
  cmsg_payload.cmsg.cmsg_type = SCM_RIGHTS;
  cmsg_payload.p.pipe_fds[0] = rdr_rd_pipe_sp->fd;
  parent_msg.msg_control = &cmsg_payload;
  parent_msg.msg_controllen = sizeof(cmsg_payload); // necessary for CMSG_FIRSTHDR to return the correct value

  cmsghdr * const cmsg = CMSG_FIRSTHDR(&parent_msg);
  assert(cmsg != nullptr);
  cmsg->cmsg_len = sizeof(cmsg_payload);

  auto bytes_sent = sendmsg(socket_fd_sp->fd, &parent_msg, 0); line_nbr = __LINE__;
  if (bytes_sent < 0) {
    const char err_msg_fmt[] =
        "%d: %s() -> sendmsg(): failed sending i/o pipe read fd{%d} datagram via named socket %s:\n\t%s";
    auto err_msg = format2str(err_msg_fmt, line_nbr, func_name, rdr_rd_pipe_sp->fd,
                              uds_socket_name.c_str(), strerror(errno));
    throw open_write_pipe_exception{ std::move(err_msg) };
  }
  log(LL::DEBUG, "%s(): ***** sent i/o pipe read fd{%d} datagram via named socket %s *****\n",
      func_name, cmsg_payload.p.pipe_fds[0], uds_socket_name.c_str());

  return rdr_wr_pipe_sp; // returning i/o pipe write fd
}

std::tuple<fd_wrapper_sp_t, fd_wrapper_sp_t, fd_wrapper_sp_t> open_react_anon_pipes(
    string_view const uds_socket_name, int &rc)
{
  static const char* const func_name = __FUNCTION__;
  rc = EXIT_SUCCESS;

  int line_nbr = __LINE__ + 1;
  auto socket_fd_sp = create_uds_socket([uds_socket_name, &line_nbr](int err_no) -> std::string {
    const char err_msg_fmt[] = "%d: %s() -> create_uds_socket(): failed creating uds socket for use with name %s:\n\t%s";
    return format2str(err_msg_fmt, line_nbr, func_name, uds_socket_name.c_str(), strerror(err_no));
  });

  auto pipe_fds = make_anon_pipe();

  fd_wrapper_t rdr_rd_pipe{ std::get<PIPES::READ>(pipe_fds) };
  fd_wrapper_sp_t rdr_rd_pipe_sp{ &rdr_rd_pipe, &fd_cleanup_no_delete };
  fd_wrapper_sp_t rdr_wr_pipe_sp{ new fd_wrapper_t{ std::get<PIPES::WRITE>(pipe_fds), uds_socket_name.c_str() },
                                  &fd_cleanup_with_delete };

  pipe_fds = make_anon_pipe();

  fd_wrapper_t err_rd_pipe{ std::get<PIPES::READ>(pipe_fds) };
  fd_wrapper_sp_t err_rd_pipe_sp{ &rdr_rd_pipe, &fd_cleanup_no_delete };
  fd_wrapper_sp_t err_wr_pipe_sp{ new fd_wrapper_t{ std::get<PIPES::WRITE>(pipe_fds), uds_socket_name.c_str() },
                                  &fd_cleanup_with_delete };

  pipe_fds = make_anon_pipe();

  fd_wrapper_sp_t wrt_rd_pipe_sp{ new fd_wrapper_t{ std::get<PIPES::READ>(pipe_fds), uds_socket_name.c_str() },
                                  &fd_cleanup_with_delete };
  fd_wrapper_t wrt_wr_pipe{ std::get<PIPES::WRITE>(pipe_fds) };
  fd_wrapper_sp_t wrt_wr_pipe_sp{ &rdr_rd_pipe, &fd_cleanup_no_delete };

  sockaddr_un server_address{};
  socklen_t address_length;

  send_pid_and_fd_count(uds_socket_name, server_address, socket_fd_sp->fd, rdr_wr_pipe_sp->pid, 3);

  init_sockaddr(uds_socket_name, server_address, address_length);

  msghdr parent_msg{};
  memset(&parent_msg, 0, sizeof(parent_msg));
  parent_msg.msg_name = &server_address;
  parent_msg.msg_namelen = address_length;

  pipes_fds_buffer_t cmsg_payload{}; // { { 0, SOL_SOCKET, SCM_RIGHTS }, { fd1, fd2, fd3 } };
  memset(&cmsg_payload, 0, sizeof(cmsg_payload));

  cmsg_payload.cmsg.cmsg_len = 0;
  cmsg_payload.cmsg.cmsg_level = SOL_SOCKET;
  cmsg_payload.cmsg.cmsg_type = SCM_RIGHTS;
  cmsg_payload.p.pipe_fds[0] = rdr_rd_pipe_sp->fd;
  cmsg_payload.p.pipe_fds[1] = err_rd_pipe_sp->fd;
  cmsg_payload.p.pipe_fds[2] = wrt_wr_pipe_sp->fd;
  parent_msg.msg_control = &cmsg_payload;
  parent_msg.msg_controllen = sizeof(cmsg_payload); // necessary for CMSG_FIRSTHDR to return the correct value

  cmsghdr * const cmsg = CMSG_FIRSTHDR(&parent_msg);
  assert(cmsg != nullptr);
  cmsg->cmsg_len = sizeof(cmsg_payload);

  auto bytes_sent = sendmsg(socket_fd_sp->fd, &parent_msg, 0); line_nbr = __LINE__;
  if (bytes_sent < 0) {
    const char err_fmt[] =
        "%d: %s() -> sendmsg(): failed sending i/o pipes react fds{%d,%d,%d} datagram via named socket %s:\n\t%s";
    auto err_msg = format2str(err_fmt, line_nbr, func_name, rdr_rd_pipe_sp->fd, err_rd_pipe_sp->fd, wrt_wr_pipe_sp->fd,
                              uds_socket_name.c_str(), strerror(errno));
    throw open_write_pipe_exception{ std::move(err_msg) };
  }
  log(LL::DEBUG, "%s(): ***** sent i/o pipes react fds{%d,%d,%d} datagram via named socket %s *****\n",
      func_name, cmsg_payload.p.pipe_fds[0], cmsg_payload.p.pipe_fds[1], cmsg_payload.p.pipe_fds[2],
      uds_socket_name.c_str());

  return std::make_tuple( std::move(rdr_wr_pipe_sp), std::move(err_wr_pipe_sp), std::move(wrt_rd_pipe_sp));
}