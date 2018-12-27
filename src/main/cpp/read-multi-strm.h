/* read-multi-strm.h

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
#ifndef READ_MULTI_STRM_H
#define READ_MULTI_STRM_H

#include <tuple>
#include <vector>
#include <unordered_map>
#include "stream-ctx.h"

struct react_io_ctx {
  stream_ctx stdout_ctx;
  stream_ctx stderr_ctx;
  stream_ctx stdin_ctx;
  // deletes default constructor and copy constructor
  react_io_ctx() = delete;
  react_io_ctx(const react_io_ctx&) = delete;
  react_io_ctx& operator=(const react_io_ctx &) = delete;
  // supports one constructor taking arguments and a default move constructor
  // this constructor is used for emplace construction into vector
  explicit react_io_ctx(int stdout_fd, int stderr_fd, int stdin_fd)
      : stdout_ctx{stdout_fd}, stderr_ctx{stderr_fd}, stdin_ctx{stdin_fd} {}
  // supports move-only assignment semantics
  react_io_ctx(react_io_ctx &&) noexcept = default;
  react_io_ctx& operator=(react_io_ctx && rbcp) noexcept = default;
  ~react_io_ctx() = default;
  int get_stdout_fd() const { return stdout_ctx.orig_fd; }
  int get_stderr_fd() const { return stderr_ctx.orig_fd; }
  int get_stdin_fd()  const { return stdin_ctx.orig_fd;  }
};

class read_multi_stream final {
private:
  std::unordered_map<int, std::shared_ptr<react_io_ctx>> fd_map{};
  friend class stream_ctx;
public:
  read_multi_stream() = default;
  read_multi_stream(const read_multi_stream &) = delete;
  read_multi_stream& operator=(const read_multi_stream &) = delete;
  read_multi_stream& operator +=(std::tuple<int, int, int> &&react_fds);
  read_multi_stream(read_multi_stream &&rms) noexcept { this->operator=(std::move(rms)); }
  read_multi_stream& operator=(read_multi_stream &&rms) = default;
  ~read_multi_stream();
  int wait_for_io(std::vector<int> &active_fds); // borrows mutable reference to a vector of fds (returns any active)
  size_t size() const { return fd_map.size(); }
  const react_io_ctx* get_react_io_ctx(int fd) const { return lookup_react_io_ctx(fd); }
  stream_ctx* get_mutable_stream_ctx(int fd) { return lookup_mutable_stream_ctx(fd); }
  const stream_ctx* get_stream_ctx(int fd) const { return lookup_mutable_stream_ctx(fd); }
  bool remove(int fd) { return fd_map.erase(fd) > 0; }
private:
  const react_io_ctx* lookup_react_io_ctx(int fd) const;
  stream_ctx* lookup_mutable_stream_ctx(int fd) const;
  void verify_added_elem(const react_io_ctx &elem, int stdout_fd, int stderr_fd, int stdin_fd);
  void add_entry_to_map(int stdout_fd, int stderr_fd, int stdin_fd);
};

#endif //READ_MULTI_STRM_H