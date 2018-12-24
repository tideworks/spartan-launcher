/* process-cmd-dispatch-info.h

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
#ifndef SPARTAN_PROCESS_CMD_DISPATCH_INFO_H
#define SPARTAN_PROCESS_CMD_DISPATCH_INFO_H

#include <functional>
#include <unordered_set>
#include <jni.h>
#include "shm.h"

// forward declarations (dependency types for method signature declarations)
struct sessionState;

namespace cmd_dsp {

  class CmdDispatchInfoProcessor {
  private:
    JNIEnv * const env;
    const char *&class_name;
    const char *&method_name;
    jclass const cls;
    sessionState &ss;
  public:
    CmdDispatchInfoProcessor(JNIEnv *env, const char *&cls_name, const char *&meth_name, jclass cls, sessionState &ss) :
        env(env), class_name(cls_name), method_name(meth_name), cls(cls), ss(ss) {}
    shm::ShmAllocator* process_initial_cmd_dispatch_info(jbyteArray ser_cmd_dispatch_info);
  private:
#ifdef _DEBUG
    void debug_dump_dispatch_info(jobject cmd_dispatch_info);
#endif
    void apply_cmd_dsp_info_to_session_state(jobject sys_prop_strs, jobject cmd_dispatch_info);
    void extract_main_entry_method_info(jobject cmd_dispatch_info);
    jclass extract_method_info(jobject method_info, const std::function<void(std::string&, std::string&)> &action);
    void extract_method_cmd_info(jclass cmd_info_cls, jobject method_cmd_info,
                                 const std::function<void(std::string &)> &action);
    void extract_method_jvm_optns_cmd_info(jclass cmd_info_cls, jobject method_cmd_info,
                                           const std::function<void(std::string &)> &action);
  };

  void get_cmd_dispatch_info(sessionState &ss);
  std::unordered_set<std::string> get_child_processor_commands(const sessionState &ss);
}

#endif //SPARTAN_PROCESS_CMD_DISPATCH_INFO_H