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
#include <poll.h>
#include "read-multi-strm.h"
#include "signal-handling.h"
#include "format2str.h"
#include "log.h"
#include "read-on-ready.h"

#define USE_EXTRA_BROKEPIPE_DETECTION 0

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

write_result_t read_on_ready::write_to_output_stream(const pollfd_result &pollfd, FILE *const output_stream,
                                                     ullint &n_read, ullint &n_writ)
{
  static const char* const func_name = __FUNCTION__;

  // POSIX API documentation (http://man7.org/linux/man-pages/man2/pipe.2.html):
  //
  //   If a read(2) specifies a buffer size that is smaller than
  //   the next packet, then the requested number of bytes are
  //   read, and the excess bytes in the packet are discarded.
  //   Specifying a buffer size of PIPE_BUF will be sufficient to
  //   read the largest possible packets (see the previous point).
  //
  std::array<char, PIPE_BUF> iobuf_array{}; // read/write buffer
  auto const iobuf = iobuf_array.data();
  const auto iobuf_size = iobuf_array.size();
  ullint read_total{0};
  WRITE_RESULT wr{WR::NO_OP};
  std::string errmsg{};
  int line_nbr{};

  auto const handle_fd_error = [&errmsg, &line_nbr](const int fd, const int err_no) {
    errmsg = format2str("line %d: %s(): failure reading pipe fd{%d}: %s", line_nbr, func_name, fd, strerror(err_no));
  };

  fflush(output_stream);
  line_nbr = __LINE__ + 1;
  const auto output_fd = fileno(output_stream);
  if (output_fd == -1) {
    handle_fd_error(pollfd.fd, errno);
    return std::make_tuple(pollfd.fd, WR::FAILURE, std::move(errmsg));
  }

  auto const write_output = [iobuf, &read_total, &n_read, &n_writ, &line_nbr, &handle_fd_error]
      (const int in_fd, const int out_fd, const long n) -> WRITE_RESULT
  {
    read_total += n;
    n_read += n;
    line_nbr = __LINE__ + 1;
    const auto nw = write(out_fd, iobuf, (size_t) n);
    fsync(out_fd);
    if (nw > 0) {
      n_writ += nw;
    }
    if (nw != n) {
      handle_fd_error(in_fd, errno);
      return WR::FAILURE;
    }
    return WR::SUCCESS;
  };

  line_nbr = __LINE__ + 1;
  if ((pollfd.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
    const auto n = read(pollfd.fd, iobuf, sizeof(ullint));
    if (n > 0) {
      // despite fd being indicated as being in an error condition, a read() returned some data
      wr = write_output(pollfd.fd, output_fd, n);
      switch (wr) {
        // can continue
        case WR::NO_OP:
        case WR::SUCCESS:
          break;
        // can't continue
        default:
          return std::make_tuple(pollfd.fd, wr, std::move(errmsg));
      }
    } else {
      if (n == -1) {
        const auto err_no = errno;
        //
        // POSIX API documentation (http://man7.org/linux/man-pages/man2/read.2.html)
        //
        // EAGAIN The file descriptor fd refers to a file other than a socket
        //        and has been marked nonblocking (O_NONBLOCK), and the read
        //        would block.  See open(2) for further details on the
        //        O_NONBLOCK flag.
        //
        // POSIX API documentation (http://man7.org/linux/man-pages/man3/errno.3.html):
        //
        // All the error names specified by POSIX.1 must have distinct values,
        // with the exception of EAGAIN and EWOULDBLOCK, which may be the same.
        //
        // EAGAIN       Resource temporarily unavailable (may be the same
        //              value as EWOULDBLOCK) (POSIX.1-2001).
        //
        // EWOULDBLOCK  Operation would block (may be same value as EAGAIN)
        //              (POSIX.1-2001).
        //
        ///////////////////////////////////////////////////////////////////////////////
        // My inference:
        //
        // If first read(2) attempt (made after poll(2) ready-data event)
        // returns error condition of either EAGAIN (or EWOULDBLOCK), then
        // indicates a broken pipe connection.
        //
        wr = (err_no == EAGAIN || err_no == EWOULDBLOCK) ? WR::PIPE_CONN_BROKEN : WR::FAILURE;
        errmsg = format2str("line %d: %s(): failure reading pipe - error on: fd{%d}", line_nbr, func_name, pollfd.fd);
      } else /* n == 0 */ {
        wr = WR::END_OF_FILE;
      }
      return std::make_tuple(pollfd.fd, wr, std::move(errmsg));
    }
  }

  // lambda that reads from specified fd device and writes (echoes) to specified output
  // - reads from file descriptor input until it would block or is closed (or an error)
  auto const echo = [iobuf, iobuf_size, &read_total, &line_nbr, &handle_fd_error, &write_output](
      const int in_fd, const int out_fd) -> WRITE_RESULT
  {
    bool sig_intr;
    while (!(sig_intr = signal_handling::interrupted())) {
      line_nbr = __LINE__ + 1;
      const auto n = read(in_fd, iobuf, iobuf_size);
      if (n == -1) {
        const auto err_no = errno;
        if (err_no == EAGAIN || err_no == EWOULDBLOCK) {
          // if read(2) attempt made with zero bytes read so far,
          // then that indicates a broken pipe connection as the
          // poll(2) call may indicate file descriptor was ready
          // to be read but read() call returns EAGAIN|EWOULDBLOCK
          if (read_total > 0) {
            return WR::NO_OP;
          }
          handle_fd_error(in_fd, err_no);
          return WR::PIPE_CONN_BROKEN;
        }
        handle_fd_error(in_fd, err_no);
        return WR::FAILURE;
      }
      if (n <= 0) {
        fsync(out_fd);
        return WR::END_OF_FILE;
      }
      // is data in io buffer to be written to output
      auto rtn = write_output(in_fd, out_fd, n);
      switch (rtn) {
        case WR::NO_OP:
        case WR::SUCCESS:
          continue;
        default:
          return rtn; // an error condition (or end-of-file)
      }
    }
    return sig_intr ? WR::INTERRUPTED : WR::SUCCESS;
  };

  wr = echo(pollfd.fd, output_fd); // will echo read pipe data to stdout

  return std::make_tuple(pollfd.fd, wr, std::move(errmsg));
}

#if USE_EXTRA_BROKEPIPE_DETECTION
static bool is_write_pipe_broken(const int fd) {
  if (fd == -1) return false;
  struct pollfd pfd = {.fd = fd, .events = POLLERR, .revents = 0};
  if (poll(&pfd, 1, 0) < 0) return false;
  return (pfd.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0;
}
#endif

read_multi_result_t read_on_ready::multi_read_on_ready(bool &is_ctrl_z_registered, read_multi_stream &rms,
                                                       output_streams_context_map_t &output_streams_map)
{
  static const char* const func_name = __FUNCTION__;

  int ec = EXIT_SUCCESS;
  std::vector<pollfd_result> pollfds{};
  std::vector<std::future<write_result_t>> futures{};
  WRITE_RESULT wr{WR::NO_OP};
  int rc{0};

  while (rms.size() > 0 && !signal_handling::interrupted() && ((rc = rms.poll_for_io(pollfds)) == 0 || rc == EINTR)) {
    if (rc == EINTR) continue;
    wr = WR::NO_OP;
    futures.clear();
    for(const auto pollfd : pollfds) {
      auto const prbc = rms.get_mutable_stream_ctx(pollfd.fd);
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
        auto search = output_streams_map.find(pollfd.fd); // look up the file descriptor to find its output stream context
        if (search == output_streams_map.end()) {
          log(LL::WARN, "line %d: %s(): ready-to-read file descriptor failed to de-ref an output context - skipping",
              line_nbr, func_name);
          continue;
        }
        auto output_stream_ctx = search->second;
        // invoke the write to the output stream context in an asynchronous manner, using a future to get the outcome
        const auto launch_policy = pollfds.size() > 1 ? std::launch::async : std::launch::deferred;
        futures.emplace_back(
            std::async(launch_policy,
                       [pollfd, output_stream_ctx]
                       {
                         ullint n_read{0};
                         return write_to_output_stream(pollfd/*input*/, output_stream_ctx->output_stream/*output*/,
                                                       n_read, output_stream_ctx->bytes_written);
                       }));
      } else {
        // Should never reach here - indicates corrupted runtime state
        // A failed initialization detected for the stream_ctx (the input source), so remove
        // dereference key for the input and output stream context items per this file descriptor
        rms.remove(pollfd.fd); // input context
        output_streams_map.erase(pollfd.fd); // output context
        log(LL::FATAL, "line %d: %s(): stream_ctx object initialization failure per fd{%d}",
            __LINE__, func_name, pollfd.fd);
        rc = EXIT_FAILURE;
        break;
      }
    }
    // obtain results from all the async futures
    for(auto &fut : futures) {
      auto rtn = fut.get();
      const auto fd  = std::get<0>(rtn);
      auto wr2 = std::get<1>(rtn);
      switch (wr2) {
        // can continue conditions
        case WR::NO_OP:
        case WR::SUCCESS:
          break;
        // can't continue conditions
        default: {
          const react_io_ctx* const reactIoCtx = rms.get_react_io_ctx(fd);
          if (reactIoCtx != nullptr) {
            std::array<int,3> fd_s{reactIoCtx->get_stdout_fd(), reactIoCtx->get_stderr_fd(), reactIoCtx->get_stdin_fd()};
#if USE_EXTRA_BROKEPIPE_DETECTION
            if (wr2 == WR::END_OF_FILE && fd_s[2] != -1) {
              auto search = output_streams_map.find(fd_s[2]);
              if (search != output_streams_map.end()) {
                auto wrt_stream_ctx = search->second;
                const auto out_fd = fileno(wrt_stream_ctx->output_stream);
                if (is_write_pipe_broken(out_fd)) {
                  wr2 = WR::PIPE_CONN_BROKEN;
                }
              }
            }
#endif
            // remove de-reference keys for react streaming output context per this file descriptor (and related fds)
            for(const auto fd_tmp : fd_s) {
              if (fd_tmp == -1) continue;
              rms.remove(fd_tmp);
              output_streams_map.erase(fd_tmp);
            }
          }
          if (wr == WR::NO_OP) {
            wr = wr2;
          }
          if (wr2 != WR::END_OF_FILE) {
            ec = EXIT_FAILURE;
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