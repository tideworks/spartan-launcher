/* read-multi-strm.cpp

Copyright 2018 Roger D. Voss

Created by roger-dv on 4/21/18.
Modifications by roger-dv on 12/26/18

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
#include <unistd.h>
#include <cstring>
#include <future>
#include "log.h"
#include "signal-handling.h"
#include "read-multi-strm.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using namespace logger;

#define DBG_VERIFY 0

const react_io_ctx* read_multi_stream::lookup_react_io_ctx(int fd) const {
  auto search = fd_map.find(fd);
  if (search != fd_map.end()) {
    auto const &sp_entry = search->second;
    return sp_entry.get();
  }
  return nullptr;
}

stream_ctx* read_multi_stream::lookup_mutable_stream_ctx(int fd) const {
  auto search = fd_map.find(fd);
  if (search != fd_map.end()) {
    auto const &sp_entry = search->second;
    if (sp_entry->get_stdout_fd() == fd) {
      return &sp_entry->stdout_ctx;
    }
    if (sp_entry->get_stderr_fd() == fd) {
      return &sp_entry->stderr_ctx;
    }
    if (sp_entry->get_stdin_fd() == fd) {
      return &sp_entry->stdin_ctx;
    }
  }
  return nullptr;
}

void read_multi_stream::verify_added_elem(const react_io_ctx &elem, int stdout_fd, int stderr_fd, int stdin_fd) {
#if DBG_VERIFY
  assert(&fd_map.at(stdout_fd)->stdout_ctx == &elem.stdout_ctx);
  assert(&fd_map.at(stderr_fd)->stderr_ctx == &elem.stderr_ctx);
  assert(&fd_map.at(stdin_fd)->stderr_ctx == &elem.stdin_ctx);
  assert(elem.stdout_ctx.orig_fd == stdout_fd);
  assert(elem.stderr_ctx.orig_fd == stderr_fd);
  assert(elem.stdin_ctx.orig_fd == stdin_fd);
  log(LL::DEBUG,
    "\n\tadded vector element react_io_ctx: %p\n"
      "\tstdout_fd: %d, stderr_fd: %d, stdin_fd: %d",
    &elem, elem.stdout_ctx.orig_fd, elem.stderr_ctx.orig_fd, elem.stdin_ctx.orig_fd);
#endif
}

void read_multi_stream::add_entry_to_map(int stdout_fd, int stderr_fd, int stdin_fd) {
  auto sp_shared_item = std::make_shared<react_io_ctx>(stdout_fd, stderr_fd, stdin_fd);
  fd_map.insert(std::make_pair(stdout_fd, sp_shared_item));
  fd_map.insert(std::make_pair(stderr_fd, sp_shared_item));
  auto &elem = *sp_shared_item.get();
  elem.stderr_ctx.is_stderr_flag = true;
  fd_map.insert(std::make_pair(stdin_fd, sp_shared_item));
#if DBG_VERIFY
  verify_added_elem(elem, stdout_fd, stderr_fd, stdin_fd);
#endif
}

read_multi_stream& read_multi_stream::operator +=(std::tuple<int, int, int> &&react_fds) {
  int const stdout_fd = std::get<0>(react_fds);
  int const stderr_fd = std::get<1>(react_fds);
  int const stdin_fd  = std::get<2>(react_fds);

  log(LL::DEBUG, "stdout_fd: %d, stderr_fd: %d, stdin_fd: %d", stdout_fd, stderr_fd, stdin_fd);

  add_entry_to_map(stdout_fd, stderr_fd, stdin_fd);

  return *this;
}

read_multi_stream::~read_multi_stream() {
  log(LL::DEBUG, "<< (%p)->%s()", this, __FUNCTION__);
}

int read_multi_stream::wait_for_io(std::vector<int> &active_fds) {
  int rc = 0;
  fd_set rfd_set{0};
  struct timeval tv{5, 0}; // Wait up to five seconds

  FD_ZERO(&rfd_set);
  for(auto const &kv : fd_map) {
    FD_SET(kv.first, &rfd_set); // select on the original file descriptor
  }

  int highest_fd = -1;
  for(int i = 0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, &rfd_set) && i > highest_fd) {
      highest_fd = i;
    }
  }

  while(!signal_handling::interrupted()) {
    /* Watch input stream to see when it has input. */
    int line_nbr = __LINE__ + 1;
    auto ret_val = select(highest_fd + 1, &rfd_set, nullptr, nullptr, &tv);
    /* Don't rely on the value of tv now! */
    if (ret_val == -1) {
      const auto ec = errno;
      if (ec == EINTR) {
        return ec; // signal for exiting condition detected so bail out immediately
      }
      log(LL::WARN, "%d: %s() -> select(): %s", line_nbr, __FUNCTION__, strerror(ec));
      return -1;
    }

    if (ret_val > 0) {
      active_fds.clear();
      bool any_ready = false;
      for(auto const &kv : fd_map) {
        if (FD_ISSET(kv.first, &rfd_set)) {
          active_fds.push_back(kv.first);
          any_ready = true;
        }
      }
      if (any_ready) {
        logm(LL::DEBUG, "Data is available now:");
        break;
      }
    }
  }

  return rc;
}