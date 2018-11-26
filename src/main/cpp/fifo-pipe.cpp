/* fifo-pipe.cpp

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include "format2str.h"
#include "fifo-pipe.h"

static const char JLAUNCHER_FIFO_PIPE_BASENAME[] = "JLauncher_FIFO_pipe";

static volatile unsigned int seed = static_cast<unsigned>(time(nullptr));

int get_rnd_nbr(const unsigned int min_n, const unsigned int max_n) {
  auto this_seed = seed;
  auto rnd_n = rand_r(&this_seed);
  const unsigned int range = 1 + max_n - min_n;
  const unsigned int buckets = RAND_MAX / range;
  const decltype(rnd_n) limit = buckets * range;
  while (rnd_n >= limit) {
    rnd_n = rand_r(&this_seed);
  }
  seed = this_seed;
  return min_n + rnd_n / buckets;
}

// Utility function that makes a fifo pipe name
//
// throws make_fifo_pipe_name_exception on failure.
//
std::string make_fifo_pipe_name(const char * const progname, const char * const fifo_pipe_basename) {
  const auto pid = getpid();
  int strbuf_size = 256;
  char *strbuf = (char*) alloca(strbuf_size);
  do_msg_fmt: {
    int n = strbuf_size;
    n = snprintf(strbuf, (size_t) n, "%s/%s_%s_%d_%d", "/tmp", progname, fifo_pipe_basename, pid, get_rnd_nbr(1, 99));
    if (n <= 0) {
      const char * const err_msg_fmt = "%s() process %d Failed synthesizing FIFO_PIPE name string";
      auto errmsg( format2str(err_msg_fmt, __func__, pid) );
      throw make_fifo_pipe_name_exception(std::move(errmsg));
    }
    if (n >= strbuf_size) {
      strbuf = (char*) alloca(strbuf_size = ++n);
      goto do_msg_fmt; // try do_msg_fmt again
    }
  }
  return std::string(strbuf);
}

std::string make_jlauncher_fifo_pipe_name(const char * const progname) {
  return make_fifo_pipe_name(progname, JLAUNCHER_FIFO_PIPE_BASENAME);
}

// Utility function that makes a fifo pipe based
// on name as supplied as an argument.
//
// throws make_fifo_pipe_exception on failure.
//
void make_fifo_pipe(const char * const fifo_pipe_name) {
  auto n = mkfifo(fifo_pipe_name, 0666);
  if (n == -1) {
    const char * const err_msg_fmt = "%s() process %d Failed making FIFO_PIPE: %s";
    auto errmsg( format2str(err_msg_fmt, __func__, getpid(), strerror(errno)) );
    throw make_fifo_pipe_exception(std::move(errmsg));
  }
}

// Utility function that opens a fifo pipe.
//
// throws open_fifo_pipe_exception on failure.
//
int open_fifo_pipe(const char * const pathname, const int flags) {
  const int fd = open(pathname, flags);
  if (fd == -1) {
    const char * const err_msg_cstr = strerror(errno);
    const char * const err_msg_fmt = "%s() process %d Could not open FIFO pipe \"%s\":\n\t%s";
    unlink(pathname);
    auto errmsg( format2str(err_msg_fmt, __func__, getpid(), pathname, err_msg_cstr) );
    throw open_fifo_pipe_exception(std::move(errmsg));
  }
  return fd;
}

// Utility function that closes a fifo pipe.
//
// throws close_fifo_pipe_exception on failure.
//
void close_fifo_pipe(const int fd, const char * const pipename) {
  if (close(fd) == -1) {
    const char * const errmsg_cstr = strerror(errno);
    const char * const err_msg_fmt = "%s() process %d Failure closing FIFO pipe \"%s\": %s";
    unlink(pipename);
    auto errmsg(format2str(err_msg_fmt, __func__, getpid(), pipename, errmsg_cstr) );
    throw close_fifo_pipe_exception(std::move(errmsg));
  }
}
