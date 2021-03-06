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

  static std::mutex signals_guard;
  volatile sig_atomic_t quit_flag{0};
  static const char* const signal_invoked_fmt = "<< %s(sig: %d), quit_flag{%d}";

  static void signal_callback_handler(int sig) { // can be called asynchronously
    int tmp_quit_flag{};
    {
      std::unique_lock<std::mutex> lk(signals_guard);
      tmp_quit_flag = quit_flag = 1;
    }
    log(LL::DEBUG, signal_invoked_fmt, __FUNCTION__, sig, tmp_quit_flag);
  }

  static signal_handler_func_t ctrl_c_handler{signal_callback_handler};

  static void signal_callback_ctrl_c_handler(int sig) {
    assert(sig == SIGINT);
    int tmp_quit_flag{};
    {
      std::unique_lock<std::mutex> lk(signals_guard);
      ctrl_c_handler.operator()(sig);
      tmp_quit_flag = quit_flag;
    }
    log(LL::DEBUG, signal_invoked_fmt, __FUNCTION__, sig, tmp_quit_flag);
  }

  void set_signals_handler(__sighandler_t sigint_handler) {
    std::unique_lock<std::mutex> lk(signals_guard);
    quit_flag = 0;
    ctrl_c_handler = [sigint_handler](int sig){ sigint_handler(sig); };
    signal(SIGINT,  signal_callback_ctrl_c_handler);
    signal(SIGTERM, signal_callback_handler);
    signal(SIGTSTP, signal_callback_handler);
  }

  static std::mutex ctrl_z_guard;
  static int ctrl_z_handler_sig = SIGINT;
  static signal_handler_func_t ctrl_z_handler{[](int /*sig*/) { signal_callback_handler(SIGINT); }};

  static void signal_callback_ctrl_z_handler(int sig) { // can be called asynchronously
    assert(sig == SIGTSTP);
    int tmp_quit_flag{};
    {
      std::unique_lock<std::mutex> lk(ctrl_z_guard);
      auto const sav_cb = signal(ctrl_z_handler_sig, SIG_IGN);
      ctrl_z_handler.operator()(ctrl_z_handler_sig);
      signal(ctrl_z_handler_sig, sav_cb);
      tmp_quit_flag = quit_flag;
    }
    log(LL::DEBUG, signal_invoked_fmt, __FUNCTION__, sig, tmp_quit_flag);
  }

  void register_ctrl_z_handler(signal_handler_func_t &&cb) {
    register_ctrl_z_handler(SIGINT, std::move(cb));
  }

  void register_ctrl_z_handler(int sig, signal_handler_func_t &&cb) {
    std::unique_lock<std::mutex> lk(ctrl_z_guard);
    ctrl_z_handler_sig = sig;
    ctrl_z_handler = std::move(cb);
    signal(SIGTSTP, signal_callback_ctrl_z_handler);
  }

}