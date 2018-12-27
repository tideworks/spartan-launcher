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
#include "read-multi-strm.h"
#include "signal-handling.h"
#include "format2str.h"
#include "log.h"
#include "read-on-ready.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using namespace logger;

const char* write_result_str(WRITE_RESULT rslt) {
  switch (rslt) {
    case (WR) WR::SUCCESS:
      return "success";
    case (WR) WR::FAILURE:
      return "failure";
    case (WR) WR::INTERRUPTED:
      return "thread interrupted";
    case (WR) WR::END_OF_FILE:
      return "end of input stream";
    default:
      return "";
  }
}

using write_result_t = std::tuple<int, int, WRITE_RESULT, std::string>;

static write_result_t write_to_output_stream(const int fd, stream_ctx &rbc, FILE *const output_stream, ullint &n_writ)
{
  WRITE_RESULT wr{WR::NO_OP};
  std::string errmsg{};

  auto const handle_fd_error = [&errmsg](const int fd, const int err_no) {
    errmsg = format2str("failure reading pipe fd{%d} - %s", fd, strerror(err_no));
  };

  // lambda that reads from specified fd device and writes (echoes) to specified FILE stream output
  // - reads from file descriptor input until it is closed (or error)
  auto const echo = [&handle_fd_error](int in_fd, FILE *out_stream, ullint &n_read, ullint &n_writ) -> WRITE_RESULT {
    char iobuf[2048];
    bool sig_intr;
    while (!(sig_intr = signal_handling::interrupted())) {
      const auto n = read(in_fd, iobuf, sizeof(iobuf));
      if (n == -1) {
        const auto err_no = errno;
        if (err_no == EAGAIN || err_no == EWOULDBLOCK) {
          return WR::NO_OP;
        }
        handle_fd_error(in_fd, errno);
        return WR::FAILURE;
      }
      if (n <= 0) {
        fflush(out_stream);
        return WR::END_OF_FILE;
      }
      n_read += n;
      const auto nw = fwrite(iobuf, 1, (size_t) n, out_stream);
      fflush(out_stream);
      if (nw > 0) {
        n_writ += nw;
      }
      if (nw != (decltype(nw)) n) {
        return WR::FAILURE;
      }
    }
    return sig_intr ? WR::INTERRUPTED : WR::SUCCESS;
  };

  ullint n_read{0};

  wr = echo(fd, output_stream, n_read, n_writ); // will echo read pipe data to stdout

  int rc = wr == WR::FAILURE ? EXIT_FAILURE : EXIT_SUCCESS;

  return std::make_tuple(fd, rc, wr, std::move(errmsg));
}

read_multi_result_t read_on_ready(bool &is_ctrl_z_registered, read_multi_stream &rms,
                                  output_streams_context_map_t &output_streams_map)
{
  std::vector<int> fds{};
  std::vector<std::future<write_result_t>> futures{};
  WRITE_RESULT wr{WR::FAILURE};
  int rc{0};

  while (rms.size() > 0 && !signal_handling::interrupted() && (rc = rms.wait_for_io(fds)) == 0) {
    futures.clear();
    for(auto const fd : fds) {
      auto const prbc = rms.get_mutable_read_buf_ctx(fd);
      assert(prbc != nullptr); // lookup should never derefence to a null pointer
      if (prbc == nullptr) continue;
      if (prbc->is_valid_init()) {
        if (!is_ctrl_z_registered) { // a one-time-only initialization
          const auto curr_thrd = pthread_self();
          signal_handling::register_ctrl_z_handler([curr_thrd](int sig) {
            log(LL::DEBUG, "<< %s(sig: %d)", "signal_interrupt_thread", sig);
            pthread_kill(curr_thrd, sig);
          });
          is_ctrl_z_registered = true;
        }
        auto search = output_streams_map.find(fd); // look up the file descriptor to find its output stream context
        if (search == output_streams_map.end()) {
          log(LL::WARN, "line %d: %s(): ready-to-read file descriptor failed to deref an output context - skipping",
              __LINE__, __FUNCTION__);
          continue;
        }
        auto output_stream_ctx = search->second;
        // invoke the write to the output stream context in an asynchronous manner, using a future to get the outcome
        futures.emplace_back(
            std::async(std::launch::async,
                       [fd, prbc, output_stream_ctx] {
                         auto const output_stream = output_stream_ctx->output_stream;
                         auto &bytes_written = output_stream_ctx->bytes_written;
                         return write_to_output_stream(fd, *prbc, output_stream, bytes_written);
                       }));
      } else {
        // A failed initialization detected for the stream_ctx (the input source), so remove
        // dereference key for the input and output stream context items per this file descriptor
        rms.remove(fd); // input context
        output_streams_map.erase(fd); // output context
        log(LL::ERR, "initialization failure of stream_ctx object per fd{%d}", fd);
        rc = EXIT_FAILURE;
        break;
      }
    }
    // obtain results from all the async futures
    for(auto &fut : futures) {
      auto rtn = fut.get();
      auto const fd  = std::get<0>(rtn);
      auto const rc2 = std::get<1>(rtn);
      if (rc2 != EXIT_SUCCESS) {
        rc = rc2;
        wr = std::get<2>(rtn);
        // removed dereference key for output context per this file descriptor
        rms.remove(fd);
        output_streams_map.erase(fd);
        log(LL::ERR, std::get<3>(rtn).c_str());
      }
    }
  }

  if (rc == EXIT_SUCCESS) {
    wr = WR::SUCCESS;
  }
  return std::make_tuple(rc, wr);
}