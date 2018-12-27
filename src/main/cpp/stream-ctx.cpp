/* stream-ctx.cpp

Copyright 2018 Roger D. Voss

Created by roger-dv on 4/12/18.
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
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include "signal-handling.h"
#include "format2str.h"
#include "log.h"
#include "stream-ctx.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using namespace logger;

void close_dup_fd(fd_t *p) {
  if (p != nullptr && p->dup_fd >= 0) {
    close(p->dup_fd);
    p->dup_fd = -1;
  }
}

stream_ctx::stream_ctx(const int input_fd) : orig_fd{input_fd}, sp_input_fd{this, &close_dup_fd} {
  assert(orig_fd != -1);
  int line_nbr = __LINE__ + 1;
  dup_fd = dup(orig_fd); // get a dup file descriptor from original
  if (dup_fd == -1) {
    throw stream_ctx_exception( format2str("%d: %s() -> dup(): %s", line_nbr, __FUNCTION__, strerror(errno)) );
  }
  // bounds check the file descriptor against max fd set size
  assert(this->dup_fd < FD_SETSIZE);
  // set the dup file descriptor to non-blocking mode
  int flags = fcntl(dup_fd, F_GETFL, 0);
  auto rtn = fcntl(dup_fd, F_SETFL, flags | O_NONBLOCK);
  if (rtn == -1) {
    sp_input_fd.reset(nullptr);
    throw stream_ctx_exception( format2str("%d: %s() -> fcntl(): %s", line_nbr, __FUNCTION__, strerror(errno)) );
  }
}

stream_ctx &stream_ctx::operator=(stream_ctx &&rbc) noexcept {
  *const_cast<int*>(&orig_fd) = rbc.orig_fd;
  dup_fd = rbc.dup_fd;
  sp_input_fd = std::move(rbc.sp_input_fd);
  return *this;
}

stream_ctx::~stream_ctx() {
  if (is_debug_level()) {
    auto const ptr = sp_input_fd ? sp_input_fd.get() : nullptr;
    auto const ofd = ptr != nullptr ? ptr->orig_fd : -1;
    auto const dfd = ptr != nullptr ? ptr->dup_fd : -1;
    log(LL::DEBUG, "<< (%p)->%s(): orig_fd: %03d, dup_fd: %03d", this, __FUNCTION__, ofd, dfd);
  }
}