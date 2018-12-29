/* read-on-ready.h

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
#ifndef READ_ON_READY_H
#define READ_ON_READY_H

#include <tuple>
#include <unordered_map>
#include <memory>
#include "string-view.h"

struct pollfd_result;
class read_multi_stream;
using bpstd::string_view;

namespace read_on_ready {

  using WR = enum class WRITE_RESULT : char { NO_OP = 0, SUCCESS, FAILURE, INTERRUPTED, END_OF_FILE, PIPE_CONN_BROKEN };
  using write_result_t = std::tuple<int, WRITE_RESULT, std::string>;
  using read_multi_result_t = std::tuple<int, WRITE_RESULT>;
  using output_stream_context_t = struct output_stream_context;
  using output_streams_context_map_t = std::unordered_map<int, std::shared_ptr<output_stream_context_t>>;
  using ullint = unsigned long long;

  string_view write_result_str(WRITE_RESULT rslt);

  write_result_t write_to_output_stream(const pollfd_result &pollfd, FILE *const output_stream,
                                        ullint &n_read, ullint &n_writ);
  read_multi_result_t multi_read_on_ready(bool &is_ctrl_z_registered, read_multi_stream &rms,
                                          output_streams_context_map_t &output_streams_map);

  struct output_stream_context {
    FILE *const output_stream{nullptr};
    ullint bytes_written{0};
    explicit output_stream_context(FILE *stream) noexcept : output_stream{stream} {}
    output_stream_context() = delete;
    output_stream_context(output_stream_context &&) = delete;
    output_stream_context &operator=(const output_stream_context &&) = delete;
    output_stream_context(const output_stream_context &) = delete;
    output_stream_context &operator=(const output_stream_context &) = delete;
    ~output_stream_context() = default;
  };

} // read_on_ready

#endif //READ_ON_READY_H