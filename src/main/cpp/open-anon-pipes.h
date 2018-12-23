/* open-anon-pipes.h

Copyright 2018 Tideworks Technology
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
#ifndef SPARTAN_OPEN_ANON_PIPES_H
#define SPARTAN_OPEN_ANON_PIPES_H

#include "launch-program.h"

using launch_program::fd_wrapper_sp_t;
using bpstd::string_view;

launch_program::fd_wrapper_sp_t open_write_anon_pipe(string_view const uds_socket_name, int &rc);
std::tuple<fd_wrapper_sp_t, fd_wrapper_sp_t, fd_wrapper_sp_t> open_react_anon_pipes(
    string_view const uds_socket_name, int &rc);

#endif //SPARTAN_OPEN_ANON_PIPES_H