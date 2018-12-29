/* read-on-ready.cpp

Copyright 2018 Roger D. Voss

Created by roger-dv on 12/26/18.

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
#include <cstring>
#include <vector>
#include <future>
#include <unistd.h>
#include <climits>
#include <sys/ioctl.h>
#include "read-multi-strm.h"
#include "signal-handling.h"
#include "format2str.h"
#include "log.h"
#include "read-on-ready.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using namespace logger;
using read_on_ready::write_result_t;
using read_on_ready::read_multi_result_t;

string_view read_on_ready::write_result_str(WRITE_RESULT rslt) {
  switch (rslt) {
    case (WR) WR::SUCCESS:
      return "success";
    case (WR) WR::FAILURE:
      return "failure";
    case (WR) WR::INTERRUPTED:
      return "thread interrupted";
    case (WR) WR::END_OF_FILE:
      return "end of input stream";
    case (WR) WR::PIPE_CONN_BROKEN:
      return "pipe connection broken";
    default:
      return "";
  }
}

write_result_t read_on_ready::write_to_output_stream(const int input_fd, FILE *const output_stream,
                                                     ullint &n_read, ullint &n_writ)
{
  static const char* const func_name = __FUNCTION__;

  WRITE_RESULT wr{WR::NO_OP};
  std::string errmsg{};
  int line_nbr{};

  auto const handle_fd_error = [&errmsg, &line_nbr](const int fd, const int err_no) {
    errmsg = format2str("line %d: %s(): failure reading pipe fd{%d}: %s", line_nbr, func_name, fd, strerror(err_no));
  };

  // lambda that reads from specified fd device and writes (echoes) to specified FILE stream output
  // - reads from file descriptor input until it is closed (or error)
  auto const echo = [&handle_fd_error, &line_nbr](const int in_fd, FILE *const out_stream,
                                                  ullint &n_read, ullint &n_writ) -> WRITE_RESULT
  {
    fflush(out_stream);
    line_nbr = __LINE__ + 1;
    const auto out_fd = fileno(out_stream);
    if (out_fd == -1) {
      handle_fd_error(in_fd, errno);
      return WR::FAILURE;
    }

    // POSIX API documentation (http://man7.org/linux/man-pages/man2/pipe.2.html):
    //
    //   If a read(2) specifies a buffer size that is smaller than
    //   the next packet, then the requested number of bytes are
    //   read, and the excess bytes in the packet are discarded.
    //   Specifying a buffer size of PIPE_BUF will be sufficient to
    //   read the largest possible packets (see the previous point).
    //
    char iobuf[PIPE_BUF]; // read/write buffer
    ullint read_total{0};
    bool sig_intr;

    while (!(sig_intr = signal_handling::interrupted())) {
      line_nbr = __LINE__ + 1;
      const auto n = read(in_fd, iobuf, sizeof(iobuf));
      if (n == -1) {
        const auto err_no = errno;
        if (err_no == EAGAIN || err_no == EWOULDBLOCK) {
          // if read(2) attempt made with zero bytes read so far,
          // then that indicates a broken pipe connection as the
          // select(2) call may indicate file descriptor is ready
          // to be read but read() call returns EAGAIN|EWOULDBLOCK
          assert(read_total > 0);
          return read_total > 0 ? WR::NO_OP : WR::PIPE_CONN_BROKEN;
        }
        handle_fd_error(in_fd, err_no);
        return WR::FAILURE;
      }
      if (n <= 0) {
        fsync(out_fd);
        return WR::END_OF_FILE;
      }
      read_total += n;
      n_read += n;
      line_nbr = __LINE__ + 1;
      const auto nw = write(out_fd, iobuf, (size_t) n);
      fsync(out_fd);
      if (nw > 0) {
        n_writ += nw;
      }
      if (nw != (decltype(nw)) n) {
        handle_fd_error(in_fd, errno);
        return WR::FAILURE;
      }
    }
    return sig_intr ? WR::INTERRUPTED : WR::SUCCESS;
  };

  wr = echo(input_fd, output_stream, n_read, n_writ); // will echo read pipe data to stdout

  return std::make_tuple(input_fd, wr, std::move(errmsg));
}

read_multi_result_t read_on_ready::multi_read_on_ready(bool &is_ctrl_z_registered, read_multi_stream &rms,
                                                       output_streams_context_map_t &output_streams_map)
{
  static const char* const func_name = __FUNCTION__;

  int ec = EXIT_SUCCESS;
  std::vector<int> fds{};
  std::vector<std::future<write_result_t>> futures{};
  WRITE_RESULT wr{WR::NO_OP};
  int rc{0};

  while (rms.size() > 0 && !signal_handling::interrupted() && ((rc = rms.wait_for_io(fds)) == 0 || rc == EINTR)) {
    if (rc == EINTR) continue;
    wr = WR::NO_OP;
    futures.clear();
    for(auto const fd : fds) {
      auto const prbc = rms.get_mutable_stream_ctx(fd);
      assert(prbc != nullptr); // lookup should never derefence to a null pointer
      if (prbc == nullptr) continue;
      if (prbc->is_valid_init()) {
        if (!is_ctrl_z_registered) { // a one-time-only initialization
          is_ctrl_z_registered = true;
          const auto curr_thrd = pthread_self();
          signal_handling::register_ctrl_z_handler([curr_thrd](int sig) {
            log(LL::DEBUG, "<< signal_interrupt_thread(sig: %d)", sig);
            pthread_kill(curr_thrd, sig);
          });
        }
        int line_nbr = __LINE__ + 1;
        auto search = output_streams_map.find(fd); // look up the file descriptor to find its output stream context
        if (search == output_streams_map.end()) {
          log(LL::WARN, "line %d: %s(): ready-to-read file descriptor failed to de-ref an output context - skipping",
              line_nbr, func_name);
          continue;
        }
        auto output_stream_ctx = search->second;
        // invoke the write to the output stream context in an asynchronous manner, using a future to get the outcome
        const auto launch_policy = rms.size() > 1 && fds.size() > 1 ? std::launch::async : std::launch::deferred;
        futures.emplace_back(
            std::async(launch_policy,
                       [fd, output_stream_ctx, &line_nbr]
                       {
                         // Using ioctl(2) here to detect the case when select(2) has returned
                         // that file descriptor is ready to read yet there are zero bytes
                         // available to read - an indication of a broken pipe connection (i.e.,
                         // that the other end of the pipe has been closed by process going away)
                         int nbytes{0};
                         line_nbr = __LINE__ + 1;
                         if (ioctl(fd/*input*/, FIONREAD, &nbytes) == -1 || nbytes <= 0) {
                           auto errmsg = format2str("line %d: %s(): failure reading stream from fd{%d}: %s",
                                                    line_nbr, func_name, fd, strerror(errno));
                           return std::make_tuple(fd, WR::PIPE_CONN_BROKEN, std::move(errmsg));
                         }

                         ullint n_read{0};
                         return write_to_output_stream(fd/*input*/, output_stream_ctx->output_stream/*output*/,
                                                       n_read, output_stream_ctx->bytes_written);
                       }));
      } else {
        // Should never reach here - indicates corrupted runtime state
        // A failed initialization detected for the stream_ctx (the input source), so remove
        // dereference key for the input and output stream context items per this file descriptor
        rms.remove(fd); // input context
        output_streams_map.erase(fd); // output context
        log(LL::FATAL, "line %d: %s(): stream_ctx object initialization failure per fd{%d}",__LINE__, func_name, fd);
        rc = EXIT_FAILURE;
        break;
      }
    }
    // obtain results from all the async futures
    for(auto &fut : futures) {
      auto rtn = fut.get();
      const auto fd  = std::get<0>(rtn);
      const auto wr2 = std::get<1>(rtn);
      switch (wr2) {
        case WRITE_RESULT::NO_OP:
        case WRITE_RESULT::SUCCESS:
          break;
        case WRITE_RESULT::FAILURE:
        case WRITE_RESULT::INTERRUPTED:
        case WRITE_RESULT::END_OF_FILE:
        case WRITE_RESULT::PIPE_CONN_BROKEN: {
          ec = EXIT_FAILURE;
          if (wr == WR::NO_OP) {
            wr = wr2;
          }
          const react_io_ctx* const reactIoCtx = rms.get_react_io_ctx(fd);
          if (reactIoCtx != nullptr) {
            // removed de-reference key for react streaming output context per this file descriptor (and related fds)
            std::array<int,3> fd_s{reactIoCtx->get_stdout_fd(), reactIoCtx->get_stderr_fd(), reactIoCtx->get_stdin_fd()};
            for(const auto fd_tmp : fd_s) {
              rms.remove(fd_tmp);
              output_streams_map.erase(fd_tmp);
            }
          }
          if (wr == WR::FAILURE) {
            const std::string errmsg{std::move(std::get<2>(rtn))};
            log(LL::ERR, errmsg.c_str());
          }
          break;
        }
      }
    }
  }

  return std::make_tuple(ec, wr);
}