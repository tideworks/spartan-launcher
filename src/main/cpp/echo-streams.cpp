/* echo-streams.cpp

Copyright 2018 Roger D. Voss

Created by roger-dv on 12/28/18.

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

#include <atomic>
#include <wait.h>
#include <fcntl.h>
#include "read-multi-strm.h"
#include "read-on-ready.h"
#include "signal-handling.h"
#include "log.h"
#include "echo-streams.h"

using namespace logger;
using namespace launch_program;
using namespace read_on_ready;

/**
 * This function is used by Spartan client mode to handle response stream(s) processing, which
 * involves reading the input from another sub-command execution end-point and writing it to the
 * appropriate output stream(s).
 *
 * The function supports the old-style, single-response stream (e.g., Spartan.invokeCommand() API)
 * and also the react-style multi-stream scenario (e.g., Spartan.invokeCommandEx() API).
 *
 * The response stream(s) are obtained via Unix datagram socket where anonymous pipe descriptor(s)
 * are marshaled per the socket from the other end-point process into the client-mode process.
 *
 * Any error logging is done within the call context of this function so it only returns a code
 * indicating success or failure of outcome status.
 *
 * NOTE: Due to use of privately declared static variables, this function is not thread re-entrant,
 * but that is okay for this function is called only in Spartan client mode as the very last step
 * for processing response output to stdout, etc.
 *
 * @param uds_socket_name name of the Unix datagram socket where pipe file descriptors are obtained
 *        (name is supplied for any error reporting purposes)
 * @param read_fd_sp the Unix datagram socket descriptor that is read to obtain file descriptors
 * @param supervisor_pid this will be the process pid of the supervisor JVM instantiation
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int stdout_echo_response_stream(std::string const &uds_socket_name, fd_wrapper_sp_t &&read_fd_sp,
                                const pid_t supervisor_pid)
{
  static const char* const func_name = __FUNCTION__;

  // use of statics means is not thread re-entrant but that is okay for this function
  static std::atomic_flag s_sig_state_lock = ATOMIC_FLAG_INIT;
  static struct { // state needed for access in signal handler below
    std::atomic<pid_t> child_prcs_pid{0};
    std::atomic<pid_t> supervisor_pid{0};
    std::atomic_int rsp_fd{-1};
    std::atomic_int err_fd{-1};
    std::atomic_int wrt_fd{-1};
  } s_sig_state;

  int rtn{EXIT_SUCCESS};

  // obtain marshaled anonymous pipe file descriptor(s) from the other end-point process
  auto rslt = obtain_response_stream(uds_socket_name.c_str(), std::move(read_fd_sp));
  const pid_t child_prcs_pid = std::get<0>(rslt);
  fd_wrapper_sp_t sp_rsp_fd{ std::move(std::get<1>(rslt)) };
  fd_wrapper_sp_t sp_err_fd{ std::move(std::get<2>(rslt)) };
  fd_wrapper_sp_t sp_wrt_fd{ std::move(std::get<3>(rslt)) };

  // if all three anonymous pipe file descriptors present then indicates is react-style
  const bool is_extended_invoke = sp_err_fd != nullptr && sp_wrt_fd != nullptr;

  // set some state into the above declared statics to facilitate signal handling
  while (s_sig_state_lock.test_and_set(std::memory_order_acquire)) ; // acquire (spin) lock
  s_sig_state.child_prcs_pid = child_prcs_pid;
  s_sig_state.supervisor_pid = supervisor_pid;
  s_sig_state.rsp_fd = sp_rsp_fd->fd;
  if (is_extended_invoke) {
    s_sig_state.err_fd = sp_err_fd->fd;
    s_sig_state.wrt_fd = sp_wrt_fd->fd;
  }
  s_sig_state_lock.clear(std::memory_order_release); // release lock

  // register a Ctrl-C SIGINT handler that will exit the program if other end-point is a child process
  signal_handling::set_signals_handler([](int/*sig*/) {
    signal(SIGINT, SIG_IGN); // set to where will ignore any subsequent Ctrl-C SIGINT

    while (s_sig_state_lock.test_and_set(std::memory_order_acquire)) ; // acquire (spin) lock
    const auto tmp_child_prcs_pid = s_sig_state.child_prcs_pid.exchange(0);
    const auto tmp_supervisor_pid = s_sig_state.supervisor_pid.exchange(0);
    const auto tmp_rsp_fd = s_sig_state.rsp_fd.exchange(-1);
    const auto tmp_err_fd = s_sig_state.err_fd.exchange(-1);
    const auto tmp_wrt_fd = s_sig_state.wrt_fd.exchange(-1);
    s_sig_state_lock.clear(std::memory_order_release); // release lock

    if (tmp_child_prcs_pid != 0 && tmp_child_prcs_pid != tmp_supervisor_pid) {
      // close pipe file descriptors explicitly here as will be calling exit() - which bypasses C++ RIAA smart pointers
      auto const close_fd = [](const int tmp_fd)
      {
        if (tmp_fd != -1) {
          close(tmp_fd);
        }
      };
      close_fd(tmp_rsp_fd);
      fflush(stdout);
      close_fd(tmp_err_fd);
      fflush(stderr);
      close_fd(tmp_wrt_fd);

      kill(tmp_child_prcs_pid, SIGTERM); // signal the child process at other end-point to terminate

      signal(SIGINT, [](int/*sig*/){ _exit(EXIT_FAILURE); }); // any subsequent Ctrl-C SIGINT will abruptly terminate

      // now wait for the child process to terminate
      int status = 0;
      do {
        int line_nbr = __LINE__ + 1;
        if (waitpid(tmp_child_prcs_pid, &status, 0) == -1) {
          if (errno != ECHILD) {
            log(LL::ERR, "line %d: %s(): failed waiting for child process (pid:%d): %s",
                line_nbr, func_name, tmp_child_prcs_pid, strerror(errno));
          }
          break;
        }
        if (WIFSIGNALED(status) || WIFSTOPPED(status)) {
          log(LL::ERR, "line %d: %s(): interrupted waiting for child process (pid:%d)",
              line_nbr, func_name, tmp_child_prcs_pid);
          break;
        }
      } while (!WIFEXITED(status) && !WIFSIGNALED(status));

      exit(EXIT_FAILURE); // consider a Ctrl-C SIGINT interruption a failure status for process exiting
    } else if (tmp_supervisor_pid != 0) {
      logm(LL::WARN, "Ctrl-C interruption of echoing output of supervisor sub-command to stdout not allowed");
    }
  });

  // these will only be initialized and used for react-style multi-streams scenario
  fd_wrapper_t tmp_fd_wrp{-1};
  fd_wrapper_sp_t sp_dup_stdin_fd{nullptr, &fd_cleanup_no_delete};
  std::unique_ptr<FILE, decltype(&::fclose)> sp_wrt_strm{nullptr, &::fclose};

  read_multi_stream rms;
  output_streams_context_map_t output_streams_map;

  WRITE_RESULT wr{};
  string_view msg{};
  int line_nbr{};

  if (is_extended_invoke) {
    //
    // react-style handling of multi-streams per a sub-command execution,
    // e.g., such as with Spartan.invokeCommandEx() API
    //

    // for use with POSIX select(), file descriptors must be set with O_NONBLOCK
    // flag, so the stdin file descriptor is duplicated and then that descriptor
    // has that flag set and will be used with the select() OS API
    {
      line_nbr = __LINE__ + 1;
      const auto dup_stdin_fd = dup(STDIN_FILENO);
      if (dup_stdin_fd == -1) {
        log(LL::ERR, "line %d: %s(): dup() failed to duplicate stdin fd{%d}:\n\t%s",
            line_nbr, func_name, STDIN_FILENO, strerror(errno));
        return EXIT_FAILURE;
      }

      tmp_fd_wrp.fd = dup_stdin_fd;
      sp_dup_stdin_fd.reset(&tmp_fd_wrp);

      int flags = fcntl(dup_stdin_fd, F_GETFL, 0);
      line_nbr = __LINE__ + 1;
      rtn = fcntl(dup_stdin_fd, F_SETFL, flags | O_NONBLOCK);
      if (rtn == -1) {
        log(LL::ERR, "line %d: %s(): fcntl() failed setting duplicated stdin fd{%d} to non-blocking:\n\t%s",
            line_nbr, func_name, sp_dup_stdin_fd->fd, strerror(errno));
        return EXIT_FAILURE;
      }
    }

    // make a FILE* stream for writing to the the stdin stream of the other end-point process
    {
      line_nbr = __LINE__ + 1;
      auto const wrt_strm = fdopen(sp_wrt_fd->fd, "w");
      if (wrt_strm == nullptr) {
        log(LL::ERR, "line %d: %s(): fdopen() failed on other end-point stdin fd{%d} obtained via uds socket %s:\n\t%s",
            line_nbr, func_name, sp_wrt_fd->fd, uds_socket_name.c_str(), strerror(errno));
        return EXIT_FAILURE;
      }
      sp_wrt_strm.reset(wrt_strm);
      (void) sp_wrt_fd.release();
    }

    // add the react-sytle multi-streams context to a read_multi_stream object
    try {
      line_nbr = __LINE__ + 1;
      rms += std::make_tuple(sp_rsp_fd->fd, sp_err_fd->fd, sp_dup_stdin_fd->fd);
    } catch (const stream_ctx_exception &ex) {
      log(LL::ERR, "line %d: %s(): failed init read_multi_stream with fds obtained via uds socket %s:\n\t%s: %s",
          line_nbr, func_name, uds_socket_name.c_str(), ex.name(), ex.what());
      return EXIT_FAILURE;
    }

    output_streams_map.insert(std::make_pair(sp_rsp_fd->fd, std::make_shared<output_stream_context_t>(stdout)));
    output_streams_map.insert(std::make_pair(sp_err_fd->fd, std::make_shared<output_stream_context_t>(stderr)));
    output_streams_map.insert(std::make_pair(sp_dup_stdin_fd->fd,
                                             std::make_shared<output_stream_context_t>(sp_wrt_strm.get())));
  } else {
    //
    // old-style single-response stream handling per a sub-command execution,
    // e.g., such as with Spartan.invokeCommand() API
    //

    // add old-style single-response stream context to a read_multi_stream object
    try {
      line_nbr = __LINE__ + 1;
      rms += sp_rsp_fd->fd;
    } catch (const stream_ctx_exception &ex) {
      log(LL::ERR, "line %d: %s(): failed init read_multi_stream with fd obtained via uds socket %s:\n\t%s: %s",
          line_nbr, func_name, uds_socket_name.c_str(), ex.name(), ex.what());
      return EXIT_FAILURE;
    }

    output_streams_map.insert(std::make_pair(sp_rsp_fd->fd, std::make_shared<output_stream_context_t>(stdout)));
  }

  bool is_ctrl_z_registered = false;

  // now do the processing on the react-style multi-streams context
  auto const mr_rslt = multi_read_on_ready(is_ctrl_z_registered, rms, output_streams_map);
  auto const ec = std::get<0>(mr_rslt);
  wr = std::get<1>(mr_rslt);
  msg = write_result_str(wr);

  rtn = (ec == 0 || wr == WR::END_OF_FILE) ? EXIT_SUCCESS : EXIT_FAILURE;

  log(LL::DEBUG, "line %d: %s(): program exiting with status: [%d] %s", __LINE__, func_name, rtn, msg.c_str());

  // let the end-user know if appears the connection to the other end-point process got disrupted
  if (wr == WR::PIPE_CONN_BROKEN) {
    log(LL::ERR, "stream connection unexpectedly interrupted: %s", msg.c_str());
  }

  return rtn;
}