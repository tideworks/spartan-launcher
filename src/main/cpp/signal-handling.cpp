/* signal-handling.cpp

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
#include <csignal>
#include <cassert>
#include <mutex>
#include "log.h"
#include "signal-handling.h"

using namespace logger;

namespace signal_handling {

  volatile sig_atomic_t quit_flag{0};

  static void signal_callback_handler(int sig) { // can be called asynchronously
    quit_flag = 1;
    log(LL::DEBUG, "<< %s(sig: %d)", __FUNCTION__, sig);
  }

  void set_signals_handler() {
    static std::mutex guard;
    std::unique_lock<std::mutex> lk(guard);
    quit_flag = 0;
    signal(SIGINT, signal_callback_handler);
    signal(SIGTERM, signal_callback_handler);
    signal(SIGTSTP, signal_callback_handler);
  }

  static int ctrl_z_handler_sig = SIGINT;
  static ctrl_z_handler_t ctrl_z_handler{[](int /*sig*/) { signal_callback_handler(SIGINT); }};

  static void signal_callback_ctrl_z_handler(int sig) { // can be called asynchronously
    assert(sig == SIGTSTP);
    auto const sav_cb = signal(ctrl_z_handler_sig, SIG_IGN);
    ctrl_z_handler.operator()(ctrl_z_handler_sig);
    signal(ctrl_z_handler_sig, sav_cb);
    log(LL::DEBUG, "<< %s(sig: %d)", __FUNCTION__, sig);
  }

  void register_ctrl_z_handler(ctrl_z_handler_t cb) {
    register_ctrl_z_handler(SIGINT, std::move(cb));
  }

  void register_ctrl_z_handler(int sig, ctrl_z_handler_t cb) {
    static std::mutex guard;
    std::unique_lock<std::mutex> lk(guard);
    ctrl_z_handler_sig = sig;
    ctrl_z_handler = std::move(cb);
    signal(SIGTSTP, signal_callback_ctrl_z_handler);
  }

}