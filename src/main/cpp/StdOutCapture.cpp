/* StdOutCapture.cpp

Copyright 2017 Tideworks Technology
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
#include <cstring>
#include <fcntl.h>
#include <climits>
#include <mutex>
#include "format2str.h"
#include "StdOutCapture.h"

enum PIPES : short { READ = 0, WRITE = 1 };

static const char * const create_pipe_err_msg_fmt = "%s() process %d pipe() of create pipe descriptors failed: %s";
static const char * const dup_fd_err_msg_fmt      = "%s() process %d dup() of %s file descriptor failed: %s";
static const char * const dup2_fd_err_msg_fmt     = "%s() process %d dup2() onto %s file descriptor failed: %s";
static const char * const close_fd_err_msg_fmt    = "%s() process %d close() of file descriptor failed: %s";
static const char * const read_fd_err_msg_fmt     = "%s() process %d read() of pipe read descriptor failed: %s";

void StdOutCapture::cleanup(int *pfd) {
  if (pfd != nullptr && *pfd != -1) {
    if (close(*pfd) == -1) {
      auto errmsg( format2str(close_fd_err_msg_fmt, __func__, getpid(), strerror(errno)) );
      throw close_file_descriptor_exception(std::move(errmsg));
    }
  }
}

StdOutCapture::StdOutCapture() {
  sp_stdout.reset(&old_stdout);
  sp_stderr.reset(&old_stderr);
  sp_read_pipe.reset(&pipes[PIPES::READ]);
  sp_write_pipe.reset(&pipes[PIPES::WRITE]);
  if (pipe(pipes) == -1) {
    auto errmsg( format2str(create_pipe_err_msg_fmt, __func__, getpid(), strerror(errno)) );
    throw create_pipe_descriptors_exception(std::move(errmsg));
  }
  if ((old_stdout = dup(STDOUT_FILENO)) == -1) {
    auto errmsg( format2str(dup_fd_err_msg_fmt, __func__, getpid(), "stdout", strerror(errno)) );
    throw dup_file_descriptor_exception(std::move(errmsg));
  }
  if ((old_stderr = dup(STDERR_FILENO)) == -1) {
    auto errmsg( format2str(dup_fd_err_msg_fmt, __func__, getpid(), "stderr", strerror(errno)) );
    throw dup_file_descriptor_exception(std::move(errmsg));
  }
  capture_buf.reserve(2048);
}

StdOutCapture::~StdOutCapture() {
  if (is_capturing) {
    is_capturing = false;
    dup2(old_stdout, STDOUT_FILENO);
    dup2(old_stderr, STDERR_FILENO);
  }
}

void StdOutCapture::start_capture() {
  if (!is_capturing) {
    fflush(stdout);
    fflush(stderr);
    fcntl( pipes[PIPES::WRITE], F_SETFL, O_NONBLOCK );
    if (dup2( pipes[PIPES::WRITE], STDOUT_FILENO ) == -1) {
      auto errmsg( format2str(dup2_fd_err_msg_fmt, __func__, getpid(), "stdout", strerror(errno)) );
      throw dup_file_descriptor_exception(std::move(errmsg));
    }
    if (dup2( pipes[PIPES::WRITE], STDERR_FILENO ) == -1) {
      auto errmsg( format2str(dup2_fd_err_msg_fmt, __func__, getpid(), "stderr", strerror(errno)) );
      throw dup_file_descriptor_exception(std::move(errmsg));
    }
    capture_buf.clear();
    is_capturing = true;
  }
}

std::string StdOutCapture::get_capture() {
  fflush(stdout);
  fflush(stderr);
  fcntl( pipes[PIPES::READ], F_SETFL, O_NONBLOCK );
  const size_t bufSize = PIPE_BUF;
  char buf[bufSize];
  ssize_t bytesRead = 0;
  while( (bytesRead = read( pipes[PIPES::READ], buf, bufSize - 1 )) > 0 ) {
    buf[bytesRead] = 0;
    capture_buf += buf;
  }
  auto err_no = errno;
  if (bytesRead < 0 && err_no != EAGAIN) {
    auto errmsg( format2str(read_fd_err_msg_fmt, __func__, getpid(), strerror(err_no)) );
    throw read_file_descriptor_exception(std::move(errmsg));
  }
  auto rtn = std::move(capture_buf);
  capture_buf.clear();
  return rtn;
}

void StdOutCapture::stop_capture() {
  if (is_capturing) {
    is_capturing = false;
    if (dup2( old_stdout, STDOUT_FILENO ) == -1) {
      auto errmsg( format2str(dup2_fd_err_msg_fmt, __func__, getpid(), "stdout", strerror(errno)) );
      dup2( old_stderr, STDERR_FILENO );
      throw dup_file_descriptor_exception(std::move(errmsg));
    }
    if (dup2( old_stderr, STDERR_FILENO ) == -1) {
      auto errmsg( format2str(dup2_fd_err_msg_fmt, __func__, getpid(), "stderr", strerror(errno)) );
      throw dup_file_descriptor_exception(std::move(errmsg));
    }
  }
}

std::string StdOutCapture::capture_stdout_stderr(const std::function<void()> &action) {
  static std::mutex m;
  std::lock_guard<std::mutex> lk(m);
  auto const unlock_file = [](FILE *pf) {
    if (pf != nullptr) {
      funlockfile(pf);
    }
  };
  flockfile(stdout);
  std::unique_ptr<FILE, decltype(unlock_file)> sp_stdout_lock(stdout, unlock_file);
  flockfile(stderr);
  std::unique_ptr<FILE, decltype(unlock_file)> sp_stderr_lock(stderr, unlock_file);
  StdOutCapture stdoutCapt;
  stdoutCapt.start_capture();
  action();
  auto capture_str = stdoutCapt.get_capture();
  stdoutCapt.stop_capture();
  return capture_str;
}
