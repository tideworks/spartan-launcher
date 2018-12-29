/* stream-ctx.h

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
#ifndef READ_BUF_CTX_H
#define READ_BUF_CTX_H

#include <memory>
#include <string>
#include "spartan-exception.h"

using fd_t = class stream_ctx;
extern void close_dup_fd(fd_t *p);

class stream_ctx final {
private:
  const int orig_fd{-1};
  int dup_fd{-1};
  bool is_stderr_flag{false};
  friend struct react_io_ctx;
  friend class read_multi_stream;
public:
  stream_ctx() = default;
  stream_ctx(const stream_ctx &) = delete;
  stream_ctx& operator=(const stream_ctx &) = delete;
  explicit stream_ctx(const int input_fd);
  stream_ctx(stream_ctx &&rbc) noexcept {
    this->operator=(std::move(rbc));
  }
  stream_ctx& operator=(stream_ctx &&rbc) noexcept;
  ~stream_ctx();
  bool is_valid_init() const { return orig_fd >= 0 && dup_fd != -1; }
  bool is_stderr_stream() const { return is_stderr_flag; }
private:
  friend void close_dup_fd(fd_t *p);
  using fd_close_dup_t = decltype(&close_dup_fd);
  std::unique_ptr<stream_ctx, fd_close_dup_t> sp_input_fd{nullptr, &close_dup_fd};
};

DECL_EXCEPTION(stream_ctx)

#endif //READ_BUF_CTX_H