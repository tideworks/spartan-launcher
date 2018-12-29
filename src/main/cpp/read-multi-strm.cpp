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
#include <poll.h>
#include "log.h"
#include "signal-handling.h"
#include "read-multi-strm.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

// This declaration appears in assert.h and is part of stdc library definition. However,
// it is dependent upon conditional compilation controlled via NDEBUG macro definition.
// Here we are using it regardless of whether is debug or non-debug build, so declaring
// it extern explicitly.
extern "C" void __assert (const char *__assertion, const char *__file, int __line)
__THROW __attribute__ ((__noreturn__));

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
  assert(&fd_map.at(stdin_fd)->stdin_ctx == &elem.stdin_ctx);
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
  fd_map.insert(std::make_pair(stdin_fd, sp_shared_item));
  auto const p_elem = sp_shared_item.get();
  assert(p_elem != nullptr);
  auto &elem = *p_elem;
  elem.stderr_ctx.is_stderr_flag = true;
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

read_multi_stream& read_multi_stream::operator +=(int fd) {
  auto sp_shared_item = std::make_shared<react_io_ctx>(fd);
  fd_map.insert(std::make_pair(fd, sp_shared_item));
#if DBG_VERIFY
  auto const p_elem = sp_shared_item.get();
  assert(p_elem != nullptr);
  auto &elem = *p_elem;
  assert(&fd_map.at(fd)->stdout_ctx == &elem.stdout_ctx);
  assert(elem.stdout_ctx.orig_fd == fd);
  assert(elem.stderr_ctx.orig_fd == -1);
  assert(elem.stdin_ctx.orig_fd == -1);
#endif
  return *this;
}

read_multi_stream::~read_multi_stream() {
  log(LL::DEBUG, "<< (%p)->%s()", this, __FUNCTION__);
}

int read_multi_stream::poll_for_io(std::vector<pollfd_result> &active_fds) {
  active_fds.clear();
  const int time_out = 5 * 1000; // milliseconds

  // stack-allocate array of struct pollfd and zero initialize its memory space
  const auto fds_count = fd_map.size();
  if (fds_count == 0) return -1; // no file descriptors remaining to poll on
  const auto pollfd_array_size = sizeof(struct pollfd) * fds_count;
  auto const pollfd_array = (struct pollfd*) alloca(pollfd_array_size);
  memset(pollfd_array, 0, pollfd_array_size);

  // set fds to be polled as entries in pollfd_array
  // (requesting event notice of when ready to read)
  auto it = fd_map.begin();
  unsigned int i = 0, j = 0;
  for(; i < fds_count; i++) {
    if (it == fd_map.end()) break; // when reach iteration end of fd_map
    auto &pfd = pollfd_array[i];
    pfd.fd = it->first;
    pfd.events = POLLIN;
    j++;
    it++; // advance fd_map iterator to next element of fd_map
  }
  if (i != j && i != fds_count) {
    __assert("number of struct pollfd entries assigned to not equal to fd_map entries count", __FILE__, __LINE__);
  }

  /* poll input stream file descriptor(s) to see when any has read input */
  while(!signal_handling::interrupted()) {
    int line_nbr = __LINE__ + 1;
    auto ret_val = poll(pollfd_array, fds_count, time_out);
    if (ret_val == -1) {
      const auto ec = errno;
      if (ec == EINTR) {
        return ec; // signal interruption detected so bail out immediately
      }
      log(LL::ERR, "%d: %s() -> poll(): %s", line_nbr, __FUNCTION__, strerror(ec));
      __assert("poll() returned catastrophic error code condition", __FILE__, line_nbr);
    }

    if (ret_val > 0) {
      bool any_ready = false;
      for(i = 0; i < fds_count; i++) {
        auto &pfd = pollfd_array[i];
        if (pfd.revents != 0) {
          active_fds.push_back({.fd = pfd.fd, .revents = pfd.revents});
          any_ready = true;
        }
      }
      if (any_ready) {
        logm(LL::TRACE, "Data is available now:");
        break;
      }
    }
  }

  return 0;
}