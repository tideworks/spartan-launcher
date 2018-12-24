/* session-state.h

Copyright 2015 - 2018 Tideworks Technology
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
#ifndef __SESSION_STATE_H__
#define __SESSION_STATE_H__

#include <iostream>
#include <vector>
#include "spartan-exception.h"

// declare invalid_initialization_exception
DECL_EXCEPTION(invalid_initialization)

// forward declarations (dependency types for method signature and field declarations)
struct JavaVM_;
typedef JavaVM_ JavaVM;

struct JNIEnv_;
typedef JNIEnv_ JNIEnv;

namespace cmd_dsp {
  class CmdDispatchInfoProcessor;
}

enum class WhichMethod : short { NONE = 0, MAIN, GET_STATUS, SUPERVISOR_SHUTDOWN, CHILD_NOTIFY, CHILD_COMPLETION_NOTIFY,
                                 SUPERVISOR_DO_CMD, CHILD_DO_CMD, GET_CMD_DISPATCH_INFO };
using WM = WhichMethod;

// class and struct declarations
class methodDescriptorBase {
protected:
  bool isStaticMethod{true};
  WhichMethod whichMethod{WM::NONE};
protected:
  methodDescriptorBase() noexcept = default;
  explicit methodDescriptorBase(bool is_static_method, WhichMethod which) noexcept
      : isStaticMethod(is_static_method), whichMethod(which) {}
  virtual ~methodDescriptorBase() = default;
public:
  methodDescriptorBase(const methodDescriptorBase &) = delete;
  methodDescriptorBase(methodDescriptorBase &&) = delete;
  methodDescriptorBase& operator=(const methodDescriptorBase &) = delete;
  methodDescriptorBase& operator=(methodDescriptorBase &&) = delete;
public:
  bool isStatic() const { return isStaticMethod; }
  WhichMethod which_method() const { return whichMethod; }
  virtual bool empty() const = 0;
  virtual const char* c_str() const = 0;
  virtual const char* desc_str() const = 0;
  virtual const char* cmd_cstr() const = 0;
  virtual const char* jvm_optns_str() const = 0;
};

class methodDescriptor: public methodDescriptorBase {
protected:
  std::string fullMethodName;
  std::string descriptor;

public:
  methodDescriptor() = default;
  explicit methodDescriptor(const char *full_method_name, const char *descriptor, bool is_static_method,
                            WhichMethod which)
      : methodDescriptorBase(is_static_method, which), fullMethodName(full_method_name), descriptor(descriptor) {}
  explicit methodDescriptor(std::string &&full_method_name, std::string &&descriptor, bool is_static_method,
                            WhichMethod which)
      : methodDescriptorBase(is_static_method, which), fullMethodName(std::move(full_method_name)),
        descriptor(std::move(descriptor)) {}
  methodDescriptor(const methodDescriptor &md) { *this = md; }
  methodDescriptor(methodDescriptor &&md) noexcept { *this = std::move(md); }
  methodDescriptor & operator=(const methodDescriptor &md);
  methodDescriptor & operator=(methodDescriptor &&md) noexcept;
  ~methodDescriptor() override = default;
public:
  bool empty() const override { return fullMethodName.empty(); }
  const char* c_str() const override { return fullMethodName.c_str(); }
  const char* desc_str() const override { return descriptor.c_str(); }
  const char* cmd_cstr() const override { return ""; }
  const char* jvm_optns_str() const override { return ""; }

  friend std::ostream& operator << (std::ostream &os, const methodDescriptor &self);
  friend std::istream& operator >> (std::istream &is, methodDescriptor &self);
  friend class cmd_dsp::CmdDispatchInfoProcessor;
};

class methodDescriptorCmd: public methodDescriptor {
protected:
  std::string command{};
  std::string jvmOptionsCommandLine{};

public:
  methodDescriptorCmd() = default;
  explicit methodDescriptorCmd(const char *full_method_name, const char *descriptor, const char *cmd,
                               bool is_static_method, WhichMethod which)
      : methodDescriptor(full_method_name, descriptor, is_static_method, which), command(cmd) {}
  explicit methodDescriptorCmd(std::string &&full_method_name, std::string &&descriptor, std::string &&cmd,
                               bool is_static_method, WhichMethod which)
      : methodDescriptor(std::move(full_method_name), std::move(descriptor), is_static_method, which),
        command(std::move(cmd)) {}
  explicit methodDescriptorCmd(const char *full_method_name, const char *descriptor, const char *cmd,
                               const char *jvm_optns, bool is_static_method, WhichMethod which)
      : methodDescriptor(full_method_name, descriptor, is_static_method, which), command(cmd),
        jvmOptionsCommandLine(jvm_optns) {}
  explicit methodDescriptorCmd(std::string && full_method_name, std::string && descriptor, std::string && cmd,
                               std::string && jvm_optns, bool is_static_method, WhichMethod which)
      : methodDescriptor(std::move(full_method_name), std::move(descriptor), is_static_method, which),
        command(std::move(cmd)), jvmOptionsCommandLine(std::move(jvm_optns)) {}
  methodDescriptorCmd(const methodDescriptorCmd &md) { *this = md; }
  methodDescriptorCmd(methodDescriptorCmd &&md) noexcept { *this = std::move(md); }
  methodDescriptorCmd & operator=(const methodDescriptorCmd &md);
  methodDescriptorCmd & operator=(methodDescriptorCmd &&md) noexcept;
  ~methodDescriptorCmd() override = default;
public:
  const char* cmd_cstr() const override { return command.c_str(); }
  const std::string& cmd_str() const { return command; }
  const char* jvm_optns_str() const override { return jvmOptionsCommandLine.c_str(); }

  friend std::ostream& operator << (std::ostream &os, const methodDescriptorCmd &self);
  friend std::istream& operator >> (std::istream &is, methodDescriptorCmd &self);
};

struct sessionState {
private:
  static void close_libjvm(void*);
  static void cleanup_jvm(JavaVM*);
  static void cleanup_jnienv(JNIEnv*);

public:
  short int child_process_max_count{0};
  methodDescriptor spartanMainEntryPoint;
  methodDescriptor spartanGetStatusEntryPoint;
  methodDescriptor spartanSupervisorShutdownEntryPoint;
  methodDescriptor spartanChildNotifyEntryPoint;
  methodDescriptor spartanChildCompletionNotifyEntryPoint;
  methodDescriptor spartanSupervisorEntryPoint;
  methodDescriptor spartanChildProcessorEntryPoint;
  std::string spartanChildProcessorCommands;
  std::string systemClassPath;
  std::shared_ptr<std::vector<methodDescriptorCmd>> spSpartanSupervisorCommands;
  std::shared_ptr<std::vector<methodDescriptorCmd>> spSpartanChildProcessorCommands;
  std::shared_ptr<std::vector<std::string>> spSerializedSystemProperties;
  std::string spartanLoggingLevel;
  std::string jvmlib_path;
  std::unique_ptr<void,   decltype(&close_libjvm)>    libjvm_sp { nullptr, &close_libjvm };
  std::unique_ptr<JavaVM, decltype(&cleanup_jvm)>     jvm_sp    { nullptr, &cleanup_jvm };
  std::unique_ptr<JNIEnv, decltype(&cleanup_jnienv)>  env_sp    { nullptr, &cleanup_jnienv };

  sessionState() = default;
  sessionState(const char * const cfg_file, const char * const jvmlib_path);
  sessionState(sessionState & ss) = delete;
  sessionState & operator=(sessionState & ss) = delete;
  sessionState & clone_info_part(const sessionState &ss) noexcept;
  sessionState(sessionState && ss) noexcept : child_process_max_count(0) { *this = std::move(ss); }
  sessionState & operator=(sessionState && ss) noexcept;
  ~sessionState() = default;

  void create_jvm(const char *jvm_override_optns = "");

  friend std::ostream& operator << (std::ostream &os, const sessionState &self);
  friend std::istream& operator >> (std::istream &is, sessionState &self);
};

#ifdef _DEBUG
void debug_dump_sessionState(const sessionState& ss, const char edition = 'A');
#endif

#endif // __SESSION_STATE_H__