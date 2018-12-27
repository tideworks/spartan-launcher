/* signal-handling.h

Copyright 2018 Roger D. Voss

Created by roger-dv on 4/12/18.

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
#ifndef SIGNAL_HANDLING_H
#define SIGNAL_HANDLING_H

#include <cstdlib>
#include <csignal>
#include <functional>

namespace signal_handling {
  void set_signals_handler();
  using ctrl_z_handler_t = std::function<void(int)>;
  void register_ctrl_z_handler(ctrl_z_handler_t /*handler*/);
  void register_ctrl_z_handler(int /*sig*/, ctrl_z_handler_t /*handler*/);
  extern volatile sig_atomic_t quit_flag;
  inline bool interrupted() { return quit_flag != 0; }
}

#endif //SIGNAL_HANDLING_H