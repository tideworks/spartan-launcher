/* spartan.cpp

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
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>
#include <alloca.h>
#include <memory>
#include <future>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <mqueue.h>
#include <libgen.h>
#include <cxxabi.h>
#include <popt.h>
#include "string-view.h"
#include "shm.h"
#include "so-export.h"
#include "createjvm.h"
#include "fifo-pipe.h"
#include "mq-queue.h"
#include "session-state.h"
#include "send-mq-msg.h"
#include "process-cmd-dispatch-info.h"
#include "log.h"
#include "StdOutCapture.h"
#include "open-anon-pipes.h"
#include "read-on-ready.h"
#include "echo-streams.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using namespace logger;
using namespace launch_program;
using namespace read_on_ready;

static const size_t MSG_BUF_SZ = 4096;
static const string_view STOP_CMD{ "--STOP" };
static const string_view SHUTDOWN_CMD{ "--SHUTDOWN" };
static const string_view STATUS_CMD{ "--STATUS" };
static const string_view CHILD_PID_NOTIFY_CMD{ "--CHILD_PID_NOTIFY" };
static const string_view CHILD_PID_COMPLETION_NOTIFY_CMD{ "--CHILD_PID_COMPLETION_NOTIFY" };
static const string_view EXTENDED_INVOKE_CMD{ "--EXTENDED_INVOKE" };
static const string_view std_invoke_descriptor{ "([Ljava/lang/String;Ljava/io/PrintStream;)V" };
static const string_view react_invoke_descriptor{
  "([Ljava/lang/String;Ljava/io/PrintStream;Ljava/io/PrintStream;Ljava/io/InputStream;)V" };

static volatile sig_atomic_t flag = 0;
void set_exit_flag_true() {
  flag = 1; // set flag
}
static void signal_callback_handler(int /*sig*/) { // may be called asynchronously
  set_exit_flag_true();
}

// This declaration appears in assert.h and is part of stdc library definition. However,
// it is dependent upon conditional compilation controlled via NDEBUG macro definition.
// Here we are using it regardless of whether is debug or non-debug build, so declaring
// it extern explicitly.
extern "C" void __assert (const char *__assertion, const char *__file, int __line)
      __THROW __attribute__ ((__noreturn__));

inline const char* get_env_var(const char * const name, const bool do_assert = false) noexcept {
  char *val = getenv(name);
  if (val == nullptr && do_assert) {
    __assert("getenv() did not find expected environment variable to be defined on startup", __FILE__, __LINE__);
  }
  return val != nullptr ? strdup(val) : "";
}

static const char* get_executable_dir() noexcept {
  char strbuf[2048];
  auto n = readlink("/proc/self/exe", strbuf, sizeof(strbuf) - 1);
  if (n != -1) {
    strbuf[n] = '\0'; // make sure string is null terminated as C strings should be
    const char *tmpstr = nullptr;
    try {
      tmpstr = static_cast<char*>(dirname(strbuf));
      if (tmpstr == nullptr) {
        __assert("dirname() could not parse extract executable's directory path on startup", __FILE__, __LINE__);
      }
    } catch(...) {
      __assert("dirname() threw exception parsing executable's directory path on startup", __FILE__, __LINE__);
    }
    tmpstr = strdup(tmpstr);
    if (tmpstr == nullptr) {
      __assert("strdup() could not duplicate the executable's directory path on startup", __FILE__, __LINE__);
    }
    return tmpstr;
  }
  return ".";
}

static const string_view s_java_classpath{ get_env_var("CLASSPATH") }; // optional
static const string_view s_java_home_path{ get_env_var("JAVA_HOME", true) }; // must be defined
static const string_view s_executable_dir{ get_executable_dir() };
static string_view s_progpath;
static string_view s_progname;
static string_view s_jlauncher_queue_name;
static string_view s_jsupervisor_queue_name;
const char * java_classpath() { return s_java_classpath.c_str(); }
const char * java_home_path() { return s_java_home_path.c_str(); }
const char * executable_dir() { return s_executable_dir.c_str(); }
const char * progpath() { return s_progpath.c_str(); }
const char * progname() { return s_progname.c_str(); }

using processor_result_t = std::tuple<bool,int>;
using send_mq_msg_cb_t = std::function<int (const char * const msg)>;
using action_cb_t = std::function<int(sessionState& session, JavaVM * const jvm)>;

template<typename T>
using defer_jobj_sp_t = std::unique_ptr<_jobject, T>;

template<typename T>
using defer_jstr_sp_t = std::unique_ptr<_jstring, T>;

static int client_status_request(std::string const &uds_socket_name, fd_wrapper_sp_t &&socket_fd_sp,
                                 const send_mq_msg_cb_t &send_mq_msg_cb);
static int invoke_java_method(JavaVM *const jvmp, const methodDescriptorBase &method_descriptor,
                              std::array<fd_wrapper_sp_t, 3> &&fds_array,
                              int argc = 0, char **argv = nullptr, const sessionState *pss = nullptr);
inline int invoke_java_method(JavaVM *const jvmp, const methodDescriptorBase &method_descriptor,
                              int argc = 0, char **argv = nullptr, const sessionState *pss = nullptr)
{
  auto const no_op_cleanup = [](fd_wrapper_t *) {};
  return invoke_java_method(jvmp, method_descriptor, std::array<fd_wrapper_sp_t, 3>{{
                                {nullptr, no_op_cleanup}, {nullptr, no_op_cleanup}, {nullptr, no_op_cleanup} }},
                            argc, argv, pss);
}
static int  supervisor(int argc, char **argv, sessionState& session);
static void supervisor_child_processor_notify(const pid_t child_pid, const char * const command_line);
static void supervisor_child_processor_completion_notify(const siginfo_t& info);
static int  invoke_java_child_processor_notify(const char * const child_pid, const char * const command_line,
                                               JavaVM * const jvmp, const methodDescriptor &method_descriptor);
static int  invoke_java_child_processor_completion_notify(const char * const child_pid,
                                                        JavaVM * const jvmp, const methodDescriptor &method_descriptor);
static int  invoke_java_supervisor_command(int /*argc*/, char **/*argv*/, const char *const msg_arg, JavaVM *const jvmp,
                                           const methodDescriptor &method_descriptor);
static int  invoke_child_process_action(sessionState& session_mut, const char *jvm_override_optns,
                                        const action_cb_t &action);
static int  invoke_child_processor_command(int argc, char **argv, const char *const msg_arg,
                                           JavaVM *const jvmp, const methodDescriptor &method_descriptor);

static void (* const shm_allocator_cleanup)(shm::ShmAllocator*) = [](shm::ShmAllocator *p) {
  if (p != nullptr) {
    ::delete p;
#ifdef _DEBUG
    log(LL::DEBUG, "pid(%d): deleted pointer to shm::ShmAllocator: %p", getpid(), p);
#endif
  }
};
using shm_allocator_sp_t = std::unique_ptr<shm::ShmAllocator, decltype(shm_allocator_cleanup)>;
static shm_allocator_sp_t s_shm_allocator_sp(nullptr, shm_allocator_cleanup);

static int(*send_supervisor_mq_msg)(const char*) = [](const char*/*msg*/) -> int { return EXIT_SUCCESS; };
static std::function<void(int)> quit_launcher_on_term_code{ [](int status_code){ _exit(status_code); } };
static std::function<void(int)> quit_supervisor_on_term_code{ [](int/*status_code*/){} };

static int s_parent_thrd_pid = 0;
inline int get_parent_pid() { return s_parent_thrd_pid; }

inline bool icompare_pred(unsigned char a, unsigned char b) noexcept { return std::tolower(a) == std::tolower(b); }

bool icompare(const std::string &a, const std::string &b) noexcept {
  if (a.length() == b.length()) {
    return std::equal(b.begin(), b.end(),
                      a.begin(), icompare_pred);
  }
  return false;
}

extern "C" SO_EXPORT int one_time_init_main(int argc, char **argv)
{
  s_parent_thrd_pid = getpid();

  const char *tmpstr = nullptr;

  {
    tmpstr = strdup(argv[0]);
    if (tmpstr == nullptr) {
      __assert("strdup() could not duplicate program full path name on startup", __FILE__, __LINE__);
    }
    s_progpath = tmpstr;
  }

  {
    tmpstr = [](const char* const path) -> const char* {
      auto const dup_path = strdupa(path);
      return strdup(basename(dup_path));
    }(progpath());
    if (tmpstr == nullptr) {
      __assert("strdup() could not duplicate program base name on startup", __FILE__, __LINE__);
    }
    s_progname = tmpstr;
  }

  logger::set_progname(progname());
  logger::set_to_unbuffered();
  log(LL::INFO, "starting process %d", get_parent_pid());
  log(LL::DEBUG, "%d command-line arg(s),\n\tprogram path: \"%s\"\n\texecutable dir: \"%s\"",
      argc - 1, progpath(), executable_dir());

  signal(SIGINT, signal_callback_handler);

  {
    tmpstr = strdup(get_jlauncher_mq_queue_name(progname()).c_str());
    if (tmpstr == nullptr) {
      __assert("strdup() could not duplicate jlauncher_mq_queue_name on startup", __FILE__, __LINE__);
    }
    s_jlauncher_queue_name = tmpstr;
  }

  {
    tmpstr = strdup(get_jsupervisor_mq_queue_name(progname()).c_str());
    if (tmpstr == nullptr) {
      __assert("strdup() could not duplicate s_jsupervisor_queue_name on startup", __FILE__, __LINE__);
    }
    s_jsupervisor_queue_name = tmpstr;
  }

  return forkable_main_entry(argc, argv, false);
}

enum class Operation : short { NONE, SERVICE, INVOKED_COMMAND, STATUS, STOP, COMMAND };
using OP = Operation;

extern "C" SO_EXPORT int forkable_main_entry(int argc, char **argv, const bool is_extended_invoke) {
  volatile int exit_code = EXIT_SUCCESS;

  static auto const send_launcher_mq_msg = [](const char * const msg) -> int {
    if (flag != 0) return EXIT_SUCCESS; // flag when non-zero indicates was signaled to terminate
    return send_mq_msg::send_mq_msg(msg, s_jlauncher_queue_name.c_str());
  };

  send_supervisor_mq_msg = [](const char * const msg) -> int {
    if (flag != 0) return EXIT_SUCCESS; // flag when non-zero indicates was signaled to terminate
    return send_mq_msg::send_mq_msg(msg, s_jsupervisor_queue_name.c_str());
  };

  // static lambda (with closure) that is now defined to send
  // the stop message and cause the supervisor program to exit
  quit_supervisor_on_term_code = [&exit_code](int term_code) {
    exit_code = term_code;
    send_supervisor_mq_msg(STOP_CMD.c_str());
  };

  // static lambda (with closure) that is now defined to send
  // the stop message and cause the launcher program to exit
  quit_launcher_on_term_code = [&exit_code](int term_code) {
    exit_code = term_code;
    send_launcher_mq_msg(STOP_CMD.c_str());
  };

  auto const send_flattened_argv_msg = [argc, argv, is_extended_invoke](string_view const uds_socket_name,
                                                                        string_view const queue_name,
                                                                        send_mq_msg::str_array_filter_cb_t filter)-> int
  {
    std::string extended_invoke_cmd{ EXTENDED_INVOKE_CMD.c_str() };
    extended_invoke_cmd += is_extended_invoke ? "=true" : "=false";
    return send_mq_msg::send_flattened_argv_mq_msg(argc, argv, {extended_invoke_cmd.c_str(),extended_invoke_cmd.size()},
                                                   uds_socket_name, queue_name, std::move(filter));
  };

  if (argc > 1) {
    // there are command line arguments to iterate and parse
    try {
      static const string_view cfg_file{ "config.ini" };
      static const string_view srvc_optn{ "service" };
      static const string_view pipe_optn{ "pipe=" };
      static const string_view status_cmd{ "status" };
      static const string_view stop_cmd{ "stop" };
      std::string pipe_option{}, command{};
      std::string uds_socket_name_arg{};
      Operation operation = OP::NONE;

      // Iterate through command line arguments and set the operation that needs to be performed.
      // (The first encountered, recognized operation, is the one that is selected to be done;
      //  subsequent options will be regarded as arguments to the recognized operation.)
      for(int i = 1; i < argc; i++) {
        if (*(argv[i]) == '-') {
          const char * const optn = argv[i] + 1;
          if (strcasecmp(optn, srvc_optn.c_str()) == 0) {
            if (operation == OP::NONE) {
              operation = OP::SERVICE;
            }
          } else if (strncasecmp(optn, pipe_optn.c_str(), pipe_optn.size()) == 0) {
            if (operation == OP::NONE) {
              operation = OP::INVOKED_COMMAND;
              pipe_option = argv[i];
            }
          }
        } else {
          const char * const optn = argv[i];
          if (strcasecmp(optn, status_cmd.c_str()) == 0) {
            if (operation == OP::NONE) {
              operation = OP::STATUS;
              command = status_cmd.c_str();
            }
          } else if (strcasecmp(optn, stop_cmd.c_str()) == 0) {
            if (operation == OP::NONE) {
              operation = OP::STOP;
              command = stop_cmd.c_str();
            }
          } else {
            if (operation == OP::NONE) {
              operation = OP::COMMAND;
              command = optn;
            } else if (command.empty()) {
              command = optn;
            }
          }
        }
      }

      // State machine for processing the operation that was determined in for-loop above
      // (via having examined the raw command line arguments to determine which operation)
      bool do_loop = false; /***** by default will make a one-shot-pass through switch statement *****/
      do {
        switch (operation) {
          case OP::SERVICE: {
            log(LL::INFO, "started as a service");
            const std::string jvmlib_path = determine_jvmlib_path();
            sessionState session(cfg_file.c_str(), jvmlib_path.c_str());
            const auto rtn_code = supervisor(argc, argv, session);
            if (exit_code == 0) {
              exit_code = rtn_code;
            }
            break;
          }
          case OP::INVOKED_COMMAND: {
            if (command.empty()) {
              log(LL::ERR, "expected command to process but none specified so exiting");
              exit_code = 1;
            } else {
              auto delimiter = std::find(pipe_option.begin(), pipe_option.end(), '=');
              delimiter++;
              uds_socket_name_arg = std::string(delimiter, pipe_option.end());
              operation = OP::COMMAND;
              do_loop = true; /***** loop through switch again to now process the subcommand *****/
            }
            break;
          }
          case OP::STATUS: { ;
            // request a status as output to a response stream
            auto rslt = bind_uds_socket_name(command.c_str());
            exit_code = client_status_request(std::get<1>(rslt), std::move(std::get<0>(rslt)), send_supervisor_mq_msg);
            break;
          }
          case OP::STOP: { ;
            // issue a message to the parent supervisor that instructs
            // it to stop processing and do an orderly termination
            exit_code = send_launcher_mq_msg(STOP_CMD.c_str());
            break;
          }
          case OP::COMMAND: {
            do_loop = false; /***** fall out of the loop after processing the subcommand *****/

            using cmd_t = decltype(command);
            std::transform(command.begin(), command.end(), command.begin(), ::tolower);

            sessionState shm_session;
            cmd_dsp::get_cmd_dispatch_info(shm_session);
            if (!shm_session.spartanLoggingLevel.empty()) {
              auto const logging_level = logger::str_to_level(shm_session.spartanLoggingLevel.c_str());
              logger::set_level(logging_level);
            }
            auto const cmds_set(cmd_dsp::get_child_processor_commands(shm_session));
            using cmds_set_t = decltype(cmds_set);
            if (is_trace_level()) {
              for (auto const &e: cmds_set) {
                log(LL::TRACE, "set item: \"%s\"", e.c_str());
              }
            }

            // Obtain the appropriate mq queue name - is either the jlauncher queue or is the jsupervisor queue
            auto const mq_queue_name = [](cmds_set_t const &cs, cmd_t const &c) -> string_view {
              if (cs.count(c) > 0) {
                log(LL::DEBUG, "running child processor command: %s", c.c_str());
                return s_jlauncher_queue_name;
              }
              log(LL::DEBUG, "running supervisor command: %s", c.c_str());
              return s_jsupervisor_queue_name;
            }(cmds_set, command);

            // All other command line options ("option_name ...") will
            // be assumed to be a subcommand that writes a result back to
            // an output stream, so a unix datagram bind name is provided
            // as the first argument in the message sent, followed by
            // the rest of the command line; will filter out the option
            // '-pipe=%s' if it was present.
            fd_wrapper_sp_t socket_read_fd_sp{ nullptr, [](fd_wrapper_t *) {} };
            std::string uds_socket_name{};
            if (uds_socket_name_arg.empty()) {
              auto rslt = bind_uds_socket_name(command.c_str());
              socket_read_fd_sp = std::move(std::get<0>(rslt));
              uds_socket_name = std::move(std::get<1>(rslt));
            } else {
              uds_socket_name = uds_socket_name_arg;
            }
            exit_code = send_flattened_argv_msg(
                {uds_socket_name.c_str(), uds_socket_name.size()},
                {mq_queue_name.c_str(), mq_queue_name.size()},
                [](int &_argc_ref, char *_argv[]) -> void {
                  for (int j = 0; j < _argc_ref; j++) {
                    auto argv_item = _argv[j];
                    if (argv_item != nullptr) {
                      if (*argv_item == '-' && strncasecmp(++argv_item, pipe_optn.c_str(), pipe_optn.size()) == 0) {
                        for (int k = j; k < _argc_ref; k++) {
                          _argv[k] = _argv[k + 1]; // last entry or the argv array is a nullptr sentinel entry
                        }
                        _argc_ref--; // reduce the argc count for the entry just removed
                        return;
                      }
                    }
                  }
                });
            // command line was posted as message to be processed, now
            // proceed to handle the response output stream appropriately
            if (exit_code == EXIT_SUCCESS && uds_socket_name_arg.empty()) {
              // there is no caller of Spartan.InvokeCommand() APIs so handle response stream right here
              exit_code = stdout_echo_response_stream(uds_socket_name, std::move(socket_read_fd_sp),
                                                      shm_session.supervisor_pid);
            }
            break;
          }
          default: {
            log(LL::WARN, "no recognized command line operation to process so exiting");
            exit_code = 1;
            break;
          }
        }
      } while(do_loop);
    } catch(const spartan_exception& e) {
      log(LL::ERR, "process %d terminating due to:\n\t%s: %s", getpid(), e.name(), e.what());
      exit_code = 1;
    } catch(const std::exception& e) {
      const auto ex_nm = get_unmangled_name(typeid(e).name());
      log(LL::ERR, "process %d terminating due to:\n\t%s: %s", getpid(), ex_nm.c_str(), e.what());
      exit_code = 1;
    } catch(...) {
      const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
      log(LL::ERR, "process %d terminating due to unhandled exception of type %s", getpid(), ex_nm.c_str());
      exit_code = 1;
    }
  } else {
    log(LL::WARN, "no command line argument to process so exiting");
    exit_code = 1;
  }
  log(LL::INFO, "process %d exiting %s", getpid(), exit_code == 0 ? "normally" : "with error condition");
  _exit(exit_code); // do not change this to simple return - avoids side effect with Java JVM (per g++ 7.2.1)
}

// Handles --STATUS request message as supervisor process; anonymous pipe is
// obtained opened for write, status result is written to the pipe, and closed
//
// NOTE: despite clang-tidy inspection, uds_socket_name should be declared as
// is because this method is called asynchronously and should therefore own the
// storage to the string - same goes for the methodDescriptor parameter
static void supervisor_status_response(std::string uds_socket_name, JavaVM *const jvmp,
                                       const methodDescriptor meth_desc)
{
  int rc;
  log(LL::DEBUG, "%s(): open unix-datagram-socket %s for conveying pipe fd for writing",
      __func__, uds_socket_name.c_str());

  auto fd_sp = open_write_anon_pipe({uds_socket_name.c_str(), uds_socket_name.size()}, rc);
  if (rc == EXIT_SUCCESS) {
    auto const no_op_cleanup = [](fd_wrapper_t*){};
    rc = invoke_java_method(jvmp, meth_desc, std::array<fd_wrapper_sp_t, 3> {{
                            std::move(fd_sp), {nullptr, no_op_cleanup}, {nullptr, no_op_cleanup} }});
  }
  log(LL::DEBUG, "%s() returning %s", __func__, rc == EXIT_SUCCESS ? "EXIT_SUCCESS" : "EXIT_FAILURE");
}

// Issues -STATUS request command to parent supervisor - result is written
// to an anonymous output pipe, which is written to stdout by requester process
static int client_status_request(std::string const &uds_socket_name, fd_wrapper_sp_t &&socket_fd_sp,
                                 const send_mq_msg_cb_t &send_mq_msg_cb)
{
  int strbuf_size = 256;
  auto strbuf = (char*) alloca(strbuf_size);

  int n = strbuf_size;
  do_msg_fmt: {
    n = snprintf(strbuf, (size_t) n, "%s %s", STATUS_CMD.c_str(), uds_socket_name.c_str());
    if (n <= 0) {
      log(LL::ERR, "failed synthesizing %s command string", STATUS_CMD.c_str());
      return EXIT_FAILURE;
    }
    if (n >= strbuf_size) {
      strbuf = (char*) alloca(strbuf_size = ++n);
      goto do_msg_fmt; // try fmt again
    }
  }

  auto const rtn = send_mq_msg_cb(strbuf);

  return rtn == EXIT_SUCCESS ? stdout_echo_response_stream(uds_socket_name, std::move(socket_fd_sp)) : rtn;
}

inline JNIEnv* jni_attach_thread(JavaVM * const jvmp) {
  JNIEnv *envp = nullptr;
  jvmp->AttachCurrentThreadAsDaemon((void**)&envp, nullptr);
  return envp;
}

inline int jni_detach_thread(JavaVM * const jvmp, JNIEnv * const envp) {
  int ret = EXIT_SUCCESS;
  if (envp != nullptr && envp->ExceptionCheck() != JNI_FALSE) {
    ret = EXIT_FAILURE;
    const auto excptn_str = StdOutCapture::capture_stdout_stderr([envp](){ envp->ExceptionDescribe(); });
    log(LL::ERR, excptn_str.c_str());
  }
  return jvmp->DetachCurrentThread() != JNI_OK ? EXIT_FAILURE : ret;
}

// utility function that invokes a Java method
static int invoke_java_method(JavaVM *const jvmp, const methodDescriptorBase &method_descriptor,
                              std::array<fd_wrapper_sp_t, 3> &&fds_array,
                              int argc, char **argv, const sessionState *pss)
{
  int ret = EXIT_SUCCESS;
  auto const detach_thread = [jvmp,&ret](JNIEnv *envp) {
    ret = jni_detach_thread(jvmp, envp);
  };
  std::unique_ptr<JNIEnv, decltype(detach_thread)> env_sp(jni_attach_thread(jvmp), detach_thread);

  // duplicate the original string to local storage that can then be modified
  const char * const fullMethodName = method_descriptor.c_str();
  const char * const method_signature = method_descriptor.desc_str();
  const bool invokeAsStatic = method_descriptor.isStatic();
  const char * class_name = strdupa(fullMethodName);
  const char * method_name = nullptr;
  static const char * const ctor_name = "<init>";
  try {
    method_name = [](const char * const stfbuf) -> const char * {
      char * str = const_cast<char*>(strrchr(stfbuf, '/'));
      if (str == nullptr || *(str + 1) == '\0')  throw 1;
      *str = '\0';  // null terminate the class name string
      return ++str; // return the method name string
    }(class_name); // get the fully qualified class/method name into two separate strings

    JNIEnv * const env = env_sp.get();

    auto const defer_jobj = [env](jobject p) {
      if (p != nullptr) {
        env->DeleteLocalRef(p);
      }
    };
    using defer_jobj_t = defer_jobj_sp_t<decltype(defer_jobj)>;

    auto const defer_jstr = [env](jstring p) {
      if (p != nullptr) {
        env->DeleteLocalRef(p);
      }
    };
    using defer_jstr_t = defer_jstr_sp_t<decltype(defer_jstr)>;

    defer_jobj_t spJargs_array(nullptr, defer_jobj);

    jobjectArray jargs = nullptr;
    if (argv != nullptr) {
      // create Java array of strings corresponding to the main(argc,argv) strings (minus the first arg)
      log(LL::DEBUG, "%s() create jobjectArray", __func__);
      auto const jstr_cls = env->FindClass("java/lang/String");
      auto const jargs_tmp = env->NewObjectArray(argc - 1, jstr_cls, nullptr);
      if (jargs_tmp == nullptr) throw 2;
      spJargs_array.reset(jargs_tmp);
      for(int i = 1, j = 0; i < argc; i++) {
        //log(LL::DEBUG, "%s() arg(%d) is: \"%s\"", __func__, i, argv[i]);
        defer_jstr_t spUtf_str(env->NewStringUTF(argv[i]), defer_jstr);
        env->SetObjectArrayElement(jargs_tmp, j++, spUtf_str.get());
      }
      jargs = jargs_tmp;
    }

    jclass const cls = env->FindClass(class_name);
    if (cls == nullptr) throw 3;
    // get the entry point method
    jmethodID const mid = invokeAsStatic ?  env->GetStaticMethodID(cls, method_name, method_signature) :
                                            env->GetMethodID(cls, method_name, method_signature);
    if (mid == nullptr) throw 4;

    if (!invokeAsStatic) {
      log(LL::DEBUG, "%s() getting Spartan object instance...", __func__);
    }

    // insure Spartan object instance is instantiated as a singleton (used for non-static method calls)
    jobject mObj = nullptr;
    static volatile std::atomic<jobject> atomicSpartanObj { nullptr };
    if (!invokeAsStatic && (mObj = atomicSpartanObj.load()) == nullptr) {
      static std::mutex guard;
      std::unique_lock<std::mutex> lk(guard);
      if ((mObj = atomicSpartanObj.load()) == nullptr) {
        auto const create_cls_obj = [cls,env,&method_name,&defer_jobj]() -> jobject {
          auto const ctor = env->GetMethodID(cls, ctor_name, "()V");
          if (ctor == nullptr) {
            method_name = ctor_name;
            throw 4;
          }
          defer_jobj_t spObj(env->NewObject(cls, ctor), defer_jobj);
          if (!spObj) throw 5;
          return env->NewGlobalRef(spObj.get());
        };
        mObj = atomicSpartanObj.compare_exchange_strong(mObj, create_cls_obj()) ? atomicSpartanObj.load() : nullptr;
        if (mObj == nullptr) throw 5;
        log(LL::DEBUG, "%s() singleton instance of %s allocated", __func__, class_name);
      }
    }

    log(LL::DEBUG, "%s() Spartan object instance is %s", __func__, mObj == nullptr ? "null" : "non-null");

    // set system class loader on current thread context
    {
      // save state of these local variables (may be referenced in caught exception handling code below)
      const char * const class_name_sav  = class_name;
      const char * const method_name_sav = method_name;

      class_name = "java/lang/ClassLoader";
      jclass const clsLdr_cls = env->FindClass(class_name);
      if (clsLdr_cls == nullptr) throw 3;
      method_name = "getSystemClassLoader";
      jmethodID const getSysClsLdr = env->GetStaticMethodID(clsLdr_cls, method_name, "()Ljava/lang/ClassLoader;");
      if (getSysClsLdr == nullptr) throw 4;

      // get system class loader
      defer_jobj_t spClsLdrObj(env->CallStaticObjectMethod(clsLdr_cls, getSysClsLdr), defer_jobj);

      class_name = "java/lang/Thread";
      jclass const thrd_cls = env->FindClass(class_name);
      if (thrd_cls == nullptr) throw 3;
      method_name = "currentThread";
      jmethodID const currThrd = env->GetStaticMethodID(thrd_cls, method_name, "()Ljava/lang/Thread;");
      if (currThrd == nullptr) throw 4;

      // get current thread object
      defer_jobj_t spCurrThrdObj(env->CallStaticObjectMethod(thrd_cls, currThrd), defer_jobj);

      method_name = "setContextClassLoader";
      jmethodID const setCntxClsLdr = env->GetMethodID(thrd_cls, method_name, "(Ljava/lang/ClassLoader;)V");
      if (setCntxClsLdr == nullptr) throw 4;

      // now set the system class loader on the current thread object as the thread's context class loader
      env->CallVoidMethod(spCurrThrdObj.get(), setCntxClsLdr, spClsLdrObj.get());

      // restore state of these local variables
      class_name  = class_name_sav;
      method_name = method_name_sav;
    }

    try {
      bool was_exception_raised = false;
      auto inner_deref_jargs_array_sp = std::move(spJargs_array);

      const auto which_method = method_descriptor.which_method();
      if (which_method == WM::GET_CMD_DISPATCH_INFO) {
        jclass const jcls = env->FindClass(class_name);
        if (jcls == nullptr) throw 3;

        jmethodID const get_cmd_dispatch_info = env->GetStaticMethodID(jcls, method_name, method_descriptor.desc_str());
        if (get_cmd_dispatch_info == nullptr) throw 4;

        sessionState ss;
        assert(pss != nullptr); // for this code logic flow, pss should not be null
        if (pss != nullptr) {
          ss.clone_info_part(*pss); // info contents of *pss copied onto ss
        }

        log(LL::DEBUG, "%s() invoking static method \"%s\"", __func__, fullMethodName);
        auto const ser_cmd_dispatch_info = env->CallStaticObjectMethod(jcls, get_cmd_dispatch_info);
        was_exception_raised = env->ExceptionCheck() != JNI_FALSE;

        if (!was_exception_raised) {
          cmd_dsp::CmdDispatchInfoProcessor processCDI(env, class_name, method_name, jcls, ss);
          auto pshm = processCDI.process_initial_cmd_dispatch_info(reinterpret_cast<jbyteArray>(ser_cmd_dispatch_info));
          s_shm_allocator_sp.reset(pshm);
        }
      } else if (which_method == WM::MAIN) {
        // invoke the static method main() entry point
        const char *const class_name_sav = class_name;
        class_name = "spartan/SpartanBase";
        jclass const spartanbase_cls = env->FindClass(class_name);
        if (spartanbase_cls == nullptr) throw 3;
        jmethodID const spartanbase_main = env->GetStaticMethodID(spartanbase_cls, method_name,
                                                                  "(Ljava/lang/String;ILjava/lang/reflect/Method;[Ljava/lang/String;)V");
        if (spartanbase_main == nullptr) throw 4;
        defer_jobj_t spMain_meth(env->ToReflectedMethod(cls, mid, JNI_TRUE), defer_jobj);
        class_name = class_name_sav;
        if (!spMain_meth) throw 4;

        defer_jstr_t spProgname_utf_str(env->NewStringUTF(progname()), defer_jstr);
        class_name = "java/lang/String{\"program name\"}";
        if (!spProgname_utf_str) throw 5;

        log(LL::DEBUG, "%s() invoking static method \"%s\"", __func__, fullMethodName);
        env->CallStaticVoidMethod(spartanbase_cls, spartanbase_main, spProgname_utf_str.get(),
                                  (jint) logger::get_level(), spMain_meth.get(), jargs);
        was_exception_raised = env->ExceptionCheck() != JNI_FALSE;
      } else {
        // invoke an instance method (unless is WM::CHILD_DO_CMD, which is static)
        switch (which_method) {
          case WM::GET_STATUS:
          case WM::CHILD_DO_CMD:
          case WM::SUPERVISOR_DO_CMD: {
            log(LL::DEBUG, "%s() prepare to invoke method taking response stream argument...", __func__);

            auto const make_and_set_fdesc = [env, &class_name, &method_name, &defer_jobj]
                () -> std::function<defer_jobj_t(int)>
            {
              static const char *const fdesc_cls_name = "java/io/FileDescriptor";
              auto const cls_fdesc = env->FindClass(fdesc_cls_name);
              if (cls_fdesc == nullptr) {
                class_name = fdesc_cls_name;
                throw 3;
              }

              auto const ctor_fdesc = env->GetMethodID(cls_fdesc, ctor_name, "()V");
              if (ctor_fdesc == nullptr) {
                class_name = fdesc_cls_name;
                method_name = ctor_name;
                throw 4;
              }

              return [env, &class_name, cls_fdesc, ctor_fdesc, &defer_jobj](const int fd) -> defer_jobj_t
              {
                // construct a new FileDescriptor
                defer_jobj_t sp_fdesc_jobj(env->NewObject(cls_fdesc, ctor_fdesc), defer_jobj);
                if (!sp_fdesc_jobj) {
                  class_name = fdesc_cls_name;
                  throw 5;
                }

                // poke the "fd" field with the file descriptor
                auto const field_fd = env->GetFieldID(cls_fdesc, "fd", "I");
                assert(field_fd != nullptr);
                if (field_fd == nullptr) throw -1;
                env->SetIntField(sp_fdesc_jobj.get(), field_fd, fd);

                return sp_fdesc_jobj;
              };
            }();

            auto const make_printstream = [env, &class_name, &method_name, &defer_jobj]
                (defer_jobj_t &&sp_fdesc_jobj) -> defer_jobj_t
            {
              static const char *const prtstrm_cls_name = "java/io/PrintStream";
              auto const cls_prtstrm = env->FindClass(prtstrm_cls_name);
              if (cls_prtstrm == nullptr) {
                class_name = prtstrm_cls_name;
                throw 3;
              }

              auto const ctor_prtstrm = env->GetMethodID(cls_prtstrm, ctor_name, "(Ljava/io/OutputStream;)V");
              if (ctor_prtstrm == nullptr) {
                class_name = prtstrm_cls_name;
                method_name = ctor_name;
                throw 4;
              }

              static const char *const file_output_strm_cls_name = "java/io/FileOutputStream";
              auto const cls_file_output_strm = env->FindClass(file_output_strm_cls_name);
              if (cls_file_output_strm == nullptr) {
                class_name = file_output_strm_cls_name;
                throw 3;
              }

              auto const ctor_file_output_strm = env->GetMethodID(cls_file_output_strm, ctor_name,
                                                                  "(Ljava/io/FileDescriptor;)V");
              if (ctor_file_output_strm == nullptr) {
                class_name = file_output_strm_cls_name;
                method_name = ctor_name;
                throw 4;
              }

              auto const f_output_strm = env->NewObject(cls_file_output_strm, ctor_file_output_strm,
                                                        sp_fdesc_jobj.get());
              if (f_output_strm == nullptr) {
                class_name = file_output_strm_cls_name;
                throw 5;
              }
              defer_jobj_t spObj_file_output_strm{f_output_strm, defer_jobj};

              // instantiate and construct a new PrintStream object
              auto const obj_prt_strm = env->NewObject(cls_prtstrm, ctor_prtstrm, spObj_file_output_strm.get());
              if (obj_prt_strm == nullptr) {
                class_name = prtstrm_cls_name;
                throw 5;
              }

              return {obj_prt_strm, defer_jobj};
            };

            auto const make_inputstream = [env, &class_name, &method_name, &defer_jobj]
                (defer_jobj_t &&sp_fdesc_jobj) -> defer_jobj_t
            {
              static const char *const file_input_strm_cls_name = "java/io/FileInputStream";
              auto const cls_file_input_strm = env->FindClass(file_input_strm_cls_name);
              if (cls_file_input_strm == nullptr) {
                class_name = file_input_strm_cls_name;
                throw 3;
              }

              auto const ctor_file_input_strm = env->GetMethodID(cls_file_input_strm, ctor_name,
                                                                 "(Ljava/io/FileDescriptor;)V");
              if (ctor_file_input_strm == nullptr) {
                class_name = file_input_strm_cls_name;
                method_name = ctor_name;
                throw 4;
              }

              // instantiate and construct a new FileInputStream object
              auto const obj_input_strm = env->NewObject(cls_file_input_strm, ctor_file_input_strm,
                                                         sp_fdesc_jobj.get());
              if (obj_input_strm == nullptr) {
                class_name = file_input_strm_cls_name;
                throw 5;
              }

              return {obj_input_strm, defer_jobj};
            };

            log(LL::DEBUG, "%s() creating PrintStream object...", __func__);
            auto spRsp_strm = make_printstream(make_and_set_fdesc((fds_array[0])->fd));
            (void) (fds_array[0]).release();

            const bool is_extended_invoke = fds_array[1] != nullptr && fds_array[2] != nullptr
                                            && react_invoke_descriptor.compare(method_signature) == 0;

            if (invokeAsStatic) {
              if (!is_extended_invoke) {
                if (std_invoke_descriptor.compare(method_signature) != 0) {
                  throw 6;
                }
                // invoking command-response method
                log(LL::DEBUG, "%s() invoking child process sub-command method \"%s\" with PrintStream",
                    __func__, fullMethodName);

                // invoke a child process sub-command with single response stream (static method entry point)
                env->CallStaticVoidMethod(cls, mid, jargs, spRsp_strm.get());
              } else {
                auto spErrOut_strm = make_printstream(make_and_set_fdesc((fds_array[1])->fd));
                (void) (fds_array[1]).release();
                auto spInput_strm = make_inputstream(make_and_set_fdesc((fds_array[2])->fd));
                (void) (fds_array[2]).release();
                log(LL::DEBUG, "%s() invoking child process sub-command method \"%s\" with react streams",
                    __func__, fullMethodName);

                // invoke a child process sub-command with three react streams (static method entry point)
                env->CallStaticVoidMethod(cls, mid, jargs, spRsp_strm.get(), spErrOut_strm.get(), spInput_strm.get());
              }
              break; // disregard checking for Java exceptions thrown by spawned child process
            }
            if (which_method == WM::GET_STATUS) {
              // invoke supervisor process status sub-command with single response stream (instance method entry point)
              env->CallVoidMethod(mObj, mid, spRsp_strm.get());
            } else {
              if (!is_extended_invoke) {
                if (std_invoke_descriptor.compare(method_signature) != 0) {
                  throw 6;
                }
                log(LL::DEBUG, "%s() invoking supervisor sub-command method \"%s\" with PrintStream",
                    __func__, fullMethodName);

                // invoke supervisor process sub-command with single response stream (instance method entry point)
                env->CallVoidMethod(mObj, mid, jargs, spRsp_strm.get());
              } else {
                auto spErrOut_strm = make_printstream(make_and_set_fdesc((fds_array[1])->fd));
                (void) (fds_array[1]).release();
                auto spInput_strm = make_inputstream(make_and_set_fdesc((fds_array[2])->fd));
                (void) (fds_array[2]).release();
                log(LL::DEBUG, "%s() invoking supervisor sub-command method \"%s\" with react streams",
                    __func__, fullMethodName);

                // invoke supervisor process sub-command with three react streams (instance method entry point)
                env->CallVoidMethod(mObj, mid, jargs, spRsp_strm.get(), spErrOut_strm.get(), spInput_strm.get());
              }
            }
            if (env->ExceptionCheck() != JNI_FALSE) {
              const auto excptn_str = StdOutCapture::capture_stdout_stderr([env]() { env->ExceptionDescribe(); });
              auto const excptn_cstr = excptn_str.c_str();
              log(LL::ERR, "process %d Java method %s() threw exception:\n%s", getpid(), fullMethodName, excptn_cstr);
            }
            break;
          }
          case WM::SUPERVISOR_SHUTDOWN: {
            log(LL::DEBUG, "%s() invoking method \"%s\"", __func__, fullMethodName);
            env->CallVoidMethod(mObj, mid);
            break;
          }
          case WM::CHILD_NOTIFY: {
            log(LL::DEBUG, "%s() invoking method \"%s\"", __func__, fullMethodName);
            const auto pid = static_cast<jint>(strtol(argv[0], nullptr, 10));
            const char *const cmd_line = argv[1];
            defer_jstr_t spUtf_str(env->NewStringUTF(cmd_line), defer_jstr);
            if (!spUtf_str) {
              class_name = "java/lang/String{\"child process command line arguments\"}";
              throw 5;
            }
            env->CallVoidMethod(mObj, mid, pid, spUtf_str.get());
            break;
          }
          case WM::CHILD_COMPLETION_NOTIFY: {
            log(LL::DEBUG, "%s() invoking method \"%s\"", __func__, fullMethodName);
            const auto pid = static_cast<jint>(strtol(argv[0], nullptr, 10));
            env->CallVoidMethod(mObj, mid, pid);
            break;
          }
          default: // do nothing
            log(LL::WARN, "%s() not valid or known method \"%s\"", __func__, fullMethodName);
            break;
        }
        was_exception_raised = env->ExceptionCheck() != JNI_FALSE;
      }
      if (was_exception_raised) {
        const auto excptn_str = StdOutCapture::capture_stdout_stderr([env]() { env->ExceptionDescribe(); });
        logm(LL::ERR, excptn_str.c_str());
        return EXIT_FAILURE;
      }
    } catch(int which) {
      throw; // let outer int catch handler process these
    } catch(...) {
      env_sp.reset(nullptr); // clean this up now as about to exit the process
      const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
      log(LL::ERR, "process %d Java method %s.%s(..) terminating due to unhandled exception of type %s",
          getpid(), class_name, method_name, ex_nm.c_str());
      _exit(EXIT_FAILURE);
    }
  } catch(int which) {
    env_sp.reset(nullptr); // clean this up now so don't get in race condition with exit of parent thread
    switch (which) {
    case 1:
      log(LL::ERR, "%s() invalid specification of method entry point \"%s\"", __func__, fullMethodName);
      break;
    case 2:
      log(LL::ERR, "%s() failed allocating Java args array for invoking \"%s\"", __func__, fullMethodName);
      break;
    case 3:
      log(LL::ERR, "%s() failed finding Java class \"%s\"",  __func__, class_name);
      break;
    case 4:
      log(LL::ERR, "%s() failed finding Java method \"%s(..)\" on class \"%s\"", __func__, method_name, class_name);
      break;
    case 5:
      log(LL::ERR, "%s() failed allocating object instance of class \"%s\"", __func__, class_name);
      break;
    case 6: {
      log(LL::ERR, "%s() invalid method descriptor for invoking as sub-command: %s\n\t%s(..)\n\tdescriptor: \"%s\"",
          __func__, method_descriptor.cmd_cstr(), fullMethodName, method_signature);
      break;
    }
    default:
      log(LL::ERR, "%s() unspecified exception invoking method \"%s(..)\"", __func__, fullMethodName);
    }
    env_sp.reset(nullptr); // clean this up prior to potentially exiting the process asynchronously
    if (getpid() == get_parent_pid()) {
      quit_launcher_on_term_code(EXIT_FAILURE); // let the parent process know to terminate
    }
    return EXIT_FAILURE;
  }
  env_sp.reset(nullptr); // clean this up now as its side effect sets the variable ret
  return ret;
}

// Ths supervisor thread context is the main thread of the parent process.
//
// This thread listens for mq messages. It will invoke either async calls
// into the Java JVM on the parent process context, or it will invoke async
// calls into a forked child process context of the Java JVM.
//
// On startup of the supervisor parent process the Java JVM is created.
//
// The Java static main() entry point method is then invoked asynchronously.
// The supervisor thread then goes into mq message listening mode.
static int supervisor(int argc, char **argv, sessionState& session) {
  static std::string mq_queue_name;
  static bool is_launcher_process = true;
  static volatile bool shutting_down = false;
  static volatile bool jvm_shutting_down = false;

  int exit_code = EXIT_SUCCESS, jvm_exit = EXIT_SUCCESS;
  std::atomic_int child_process_count = {1}; // account for the JVM main() method process
  sessionState shm_session;

  auto const waitid_on_forked_children = [](std::function<bool()> child_process_completion_proc) {
    bool done = false;
    siginfo_t info {0};
    do {
      if (waitid(P_ALL, 0, &info, WEXITED|WSTOPPED) == 0) {
        done = child_process_completion_proc();
        if (!jvm_shutting_down) {
          supervisor_child_processor_completion_notify(info);
        }
      } else {
        const auto rc = errno;
        switch(rc) {
          case 0:
            break;
          case ECHILD:
            if (flag != 0 || done) return; // flag when non-zero indicates was signaled to terminate
            log(LL::TRACE, "waitid(): %s", strerror(rc));
            break;
          case EINTR: // waidid() was interrupted by a signal so
            log(LL::INFO, "waitid(): %s", strerror(rc));
            return;   // exit the lambda and the thread context it's executing in
          default:
            log(LL::ERR, "waitid() returned on error: %s", strerror(rc));
        }
      }
    } while (!done);
  };

  struct {
    pid_t pid;
    std::thread jvm_thrd;
  }
      supervisor_jvm_context{-1};
  using supervisor_jvm_context_t = decltype(supervisor_jvm_context);

  auto const cleanup_supervisor_jvm_ctx =
      [&session, &exit_code, &jvm_exit, &child_process_count, &waitid_on_forked_children](supervisor_jvm_context_t *p)
      {
        static const char func_name[] = "cleanup_supervisor_jvm_ctx";
        (void) session.libjvm_sp.release();
        if (p != nullptr) {
          const auto curr_pid = getpid();
          if (p->pid > 0) {
            shutting_down = true;
            set_exit_flag_true();
            const auto jsupervisor_queue_name = get_jsupervisor_mq_queue_name(progname());
            exit_code = send_mq_msg::send_mq_msg(SHUTDOWN_CMD.c_str(), jsupervisor_queue_name.c_str());
            // waitid on all forked child processes - including the supervisor JVM process
            waitid_on_forked_children(std::function<bool()>([&child_process_count]() -> bool {
              return (child_process_count--) <= 0;
            }));
            mq_unlink(jsupervisor_queue_name.c_str());
            log(LL::TRACE, "unlinked mq queue '%s' - process pid(%d)", jsupervisor_queue_name.c_str(), curr_pid);
          } else if (p->pid == 0) {
            if (p->jvm_thrd.joinable()) {
              p->jvm_thrd.join();
            }
            exit_code = jvm_exit;
            (void) session.env_sp.release();
            (void) session.jvm_sp.release();
            (void) session.libjvm_sp.release();
          }
          log(LL::DEBUG, "<< %s(%d) - process pid(%d)", func_name, p->pid, curr_pid);
        }
      };

  std::unique_ptr<supervisor_jvm_context_t, decltype(cleanup_supervisor_jvm_ctx)> supervisor_jvm_context_sp(
      &supervisor_jvm_context,
      cleanup_supervisor_jvm_ctx);

  // lambda process-forks and then invokes the Java main() method entry point; this becomes
  // the parent supervisor Java program code that orchestrates everything else going on
  auto const do_main_entry_fork = [&jvm_exit, &session, &shm_session, argc, argv](pid_t &pid,
                                                                                  std::thread &jvm_thrd) -> int
  {
    int fork_exit_code = EXIT_SUCCESS;
    pid = fork();
    if (pid == -1) {
      fork_exit_code = EXIT_FAILURE; // fork failed so harvest the result code and return that to caller and then terminate
      log(LL::ERR, "pid(%d): fork() of Java main() entry point failed: %s", getpid(), strerror(errno));
    } else if (pid != 0) {
      mq_queue_name = get_jlauncher_mq_queue_name(progname());
      log(LL::TRACE, "jlauncher pid(%d): successfully forked Java main() entry point child process %d", getpid(), pid);
    } else {
      is_launcher_process = false;
      signal(SIGINT, SIG_IGN);
      mq_queue_name = get_jsupervisor_mq_queue_name(progname());
      session.supervisor_pid = getpid();
      session.create_jvm(); // create the Java JVM for the supervisor process
      signal(SIGINT, [](int){
        jvm_shutting_down = true;
        signal(SIGINT, SIG_IGN);
      });

      std::promise<void> prom;
      auto fut = prom.get_future();

      auto const invoke_jvm_entrypoint = [&jvm_exit, &session, argc, argv](std::promise<void> prom_rref) {

        auto const action = [&prom_rref, argc, argv](sessionState &session_param, JavaVM *const jvm) -> int {
          methodDescriptor obtainSerializedAnnotationInfo(
                                          "spartan/CommandDispatchInfo/obtainSerializedSysPropertiesAndAnnotationInfo",
                                          "()[B",
                                          true, WM::GET_CMD_DISPATCH_INFO);
          auto rc = invoke_java_method(jvm, obtainSerializedAnnotationInfo, argc, argv, &session_param);
          shm_allocator_sp_t sp_shm_alloc = std::move(s_shm_allocator_sp);
          if (rc != EXIT_SUCCESS) {
            prom_rref.set_value(); // Send notification to waiting caller thread to proceed
            return rc;
          }

          sessionState shm_session_tmp;
          cmd_dsp::get_cmd_dispatch_info(shm_session_tmp);

          if (is_trace_level()) {
            log(LL::TRACE, "jsupervisor pid(%d): Java main() method entry point (post Java annotation scan)\n"
                           "\tspartanMainEntryPoint: \"%s\"\n\tjvmlib_path: \"%s\"",
                getpid(), shm_session_tmp.spartanMainEntryPoint.c_str(), shm_session_tmp.jvmlib_path.c_str());
          }

          if (shm_session_tmp.spartanMainEntryPoint.empty()) {
            log(LL::ERR, "jsupervisor pid(%d): Java main() method entry point not specified", getpid());
            prom_rref.set_value(); // Send notification to waiting caller thread to proceed
            return EXIT_FAILURE;
          }

          prom_rref.set_value(); // Send notification to waiting caller thread to proceed

          return invoke_java_method(jvm, shm_session_tmp.spartanMainEntryPoint, argc, argv);
        };

        jvm_exit = invoke_child_process_action(session, "", action);
        log(LL::TRACE, "returning from Java main() - supervisor jvm process pid(%d)", getpid());
      };

      // asynchronously invoke the processing logic of the forked child process
      std::packaged_task<void(std::promise<void>&&)> async_invoke_jvm_entrypoint { invoke_jvm_entrypoint };
      std::thread thrd { std::move(async_invoke_jvm_entrypoint), std::move(prom) };
      jvm_thrd = std::move(thrd);

      fut.wait(); // Wait until the async invoke_jvm_entrypoint() lambda sends notification to proceed

      cmd_dsp::get_cmd_dispatch_info(shm_session);
      shm_session.libjvm_sp = std::move(session.libjvm_sp);
      shm_session.jvm_sp = std::move(session.jvm_sp);
      session.env_sp.reset(nullptr);
    }
    return fork_exit_code;
  };

  const auto rslt = do_main_entry_fork(supervisor_jvm_context.pid, supervisor_jvm_context.jvm_thrd);
  log(LL::TRACE, "process %d do_main_entry_fork() returned %d; forked child process %d",
      getpid(), rslt, supervisor_jvm_context.pid);
  if (rslt != EXIT_SUCCESS) return rslt;

  int try_attempts = 2;
try_again:
  // open mq queue and place into smart pointer for RAII
  mq_attr attr = { 0, 10, MSG_BUF_SZ, 0 };
  const auto mqd = send_mq_msg::mq_open_ex(mq_queue_name.c_str(), O_CREAT | O_EXCL | O_RDONLY, 0662, &attr);
  struct wrp_mqd_t {
    const mqd_t _mqd{-1};
    const pid_t _pid{-1};
    explicit wrp_mqd_t(mqd_t mqd1, __pid_t pid1) noexcept : _mqd(mqd1), _pid(pid1) {}
  };
  struct wrp_mqd_t *ptr;
  using wrp_mqd_ptr_t = decltype(ptr);
  auto const unlink_mqd = [](wrp_mqd_ptr_t p) {
    if (p != nullptr) {
      const auto _mqd = p->_mqd;
      const auto _pid = p->_pid;
      ::delete p;
      if (_mqd != -1) {
        mq_close(_mqd);
      }
      mq_unlink(mq_queue_name.c_str());
      log(LL::TRACE, "unlinked mq queue '%s' - process pid(%d)", mq_queue_name.c_str(), _pid);
    }
  };
  std::unique_ptr<wrp_mqd_t, decltype(unlink_mqd)> mqd_sp(new wrp_mqd_t{mqd, getpid()}, unlink_mqd);
  if (mqd_sp->_mqd == -1) {
    log(LL::ERR, "mq_open_ex('%s') failed(%d): %s", mq_queue_name.c_str(), errno, strerror(errno));
    if (errno == EEXIST) { // check if was name exists error - unlink the name as was orphaned
      mqd_sp.reset(nullptr);
      mqd_sp.reset(new wrp_mqd_t{mqd, getpid()});
      log(LL::ERR, "'%s' name existed therefore was orphaned; was unlinked, trying again...", mq_queue_name.c_str());
    }
    if (--try_attempts > 0) {
      goto try_again;
    }
    return EXIT_FAILURE;
  }
  mq_getattr(mqd_sp->_mqd, &attr);
  log(LL::TRACE, "mq_flags %ld, max_msgs %ld, msg_size %ld, curr_msgs %ld\n\t\t mq queue name '%s'",
      attr.mq_flags, attr.mq_maxmsg, attr.mq_msgsize, attr.mq_curmsgs, mq_queue_name.c_str());

  // state variables for the C++11 msg work queue - the msg will be the
  // command line args of a forked child process Java method invocation
  // or a Java method invocation on the supervisor
//  static std::mutex m;
  static std::condition_variable cv;
  static std::timed_mutex qm;
  static std::condition_variable_any qcv;

  const int child_process_max_count = is_launcher_process ? session.child_process_max_count : 100;
  std::atomic_bool qready { false };
  std::queue<std::string> dispatch_msg_queue;
  // map collection of child process groups - first child process of a command starts a group
  std::unordered_map<std::string,pid_t> prcs_grps;

  // lambda is a completion routine invoked when a forked child process terminates
  auto const child_process_completion = [&, child_process_max_count]() -> bool {
    std::unique_lock<std::timed_mutex> lk(qm, std::defer_lock);
    if (lk.try_lock()) {
      if ((child_process_count--) < child_process_max_count && !dispatch_msg_queue.empty()) {
        qready.store(true);
        qcv.notify_one();
      }
    } else {
      child_process_count--; // if can't get lock, at least do atomic decrement of the child process count
    }
    return shutting_down;
  };

  // lambda executes on a dedicated thread that does waitid() on forked child processes; when
  // detects child process termination, invokes supervisor_child_processor_completion_notify()
  if (is_launcher_process) {
    std::promise<void> prom;
    auto sf = prom.get_future().share();

    std::thread waitid_on_forked_children_thrd{
        std::function<void()>([&child_process_completion, &waitid_on_forked_children, sf] {
          sf.wait(); // Waits for calling thread to send notification to proceed
          waitid_on_forked_children(std::function<bool()>(child_process_completion));
        })};

    waitid_on_forked_children_thrd.detach();

    // Send notification to async waitid_on_forked_children() lambda to proceed
    prom.set_value();
  }

  using handle_dispatch_msg_t = std::function<void(const char * const)>;

  // lambda that does the work of forking a child process from launcher process context
  handle_dispatch_msg_t const handle_launcher_msg = [argc, argv, &session, &mqd_sp, &prcs_grps](const char *const msg) {
    static const char func_name[] = "handle_launcher_msg";

    get_rnd_nbr(1, 99); // jiggle the random number seed value (each forked child gets different seed)

    const std::string msg_str(msg);
    std::string cmd = [](const char * const msg_cstr) -> std::string {
      auto const msg_dup = strdupa(msg_cstr);
      static const char * const delim = " ";
      char *save = nullptr;
      strtok_r(msg_dup, delim, &save); // 1st arg - extended-invoke-command (skipping it)
      strtok_r(nullptr, delim, &save); // 2nd arg - unix datagram name (skipping it)
      std::string cmd_str(strtok_r(nullptr, delim, &save)); // 3rd arg is sub-command token
      cmd_str.erase(std::remove(cmd_str.begin(), cmd_str.end(), '"'), cmd_str.end());
      return cmd_str;
    }(msg);

    // does an async fork to produce a child worker process
    const pid_t pid = fork();
    if (pid == -1) {
      log(LL::ERR, "pid(%d): fork() operation of child process failed: %s\n\tfor command line: '%s'",
          getpid(), strerror(errno), msg_str.c_str());
    } else if (pid != 0) {
      log(LL::DEBUG, "child process (pid:%d) command string is: '%s'", pid, cmd.c_str());
      auto search = prcs_grps.find(cmd);
      if (search == prcs_grps.end()) {
        // not found so make new entry
        setpgid(pid, pid); // establishes new process group for this command
        prcs_grps.emplace(std::move(cmd), pid); // associate command to its process group pgid
      } else {
        // set the new child process to process group of this command
        const pid_t pgid = search->second;
        setpgid(pid, pgid);
      }
      // will inform Java main() program of forked child process
      supervisor_child_processor_notify(pid, msg_str.c_str());
    } else {
      (void) mqd_sp.release(); // don't want a forked child process to unlink the mq queue

      sessionState shm_session_st;
      cmd_dsp::get_cmd_dispatch_info(shm_session_st);
      shm_session_st.libjvm_sp = std::move(session.libjvm_sp);

      auto pMethDesc = &shm_session_st.spartanChildProcessorEntryPoint; // default child processor command entry point

      if (shm_session_st.spSpartanChildProcessorCommands) { // check for matching annotated child command method entry
        log(LL::TRACE, "@@@@ pid(%d): retrieved shm_session_st to invoke child process command: %s\n"
                       "\tchild cmd vec size: %lu",
            getpid(), cmd.c_str(), shm_session_st.spSpartanChildProcessorCommands->size());
        for (auto &methDesc : *shm_session_st.spSpartanChildProcessorCommands) {
          if (icompare(methDesc.cmd_str(), cmd)) {
            pMethDesc = &methDesc;
            break;
          }
        }
      }

      auto const jvm_override_optns = pMethDesc != nullptr ? pMethDesc->jvm_optns_str() : "";

      auto const action = [argc, argv, pMethDesc, &msg_str](sessionState &session_param, JavaVM *const jvm) -> int {
        auto const cmsg = msg_str.c_str();
        if (pMethDesc != nullptr && !pMethDesc->empty()) {
          // call the standard entry point for child commands
          return invoke_child_processor_command(argc, argv, cmsg, jvm, *pMethDesc);
        }
        log(LL::ERR, "%s(): no Java method defined to handle command line:\n\t'%s'", func_name, cmsg);
        return EXIT_FAILURE;
      };

      // invoke the processing logic of the forked child process and return its exit code
      const auto exit_rtn_code = invoke_child_process_action(shm_session_st, jvm_override_optns, action);
      log(LL::DEBUG, "<< %s() - exiting process pid(%d)", func_name, getpid());
      exit(exit_rtn_code);
    }
  };

  // lambda that does the work of invoking a supervisor method
  handle_dispatch_msg_t const handle_supervisor_msg =
      [&child_process_count, &shm_session, argc, argv](const char * const msg)
  {
    static const char func_name[] = "handle_supervisor_msg";
    child_process_count--;
    auto const msg_dup = strdupa(msg); // create a local copy of string that can be mutated

    static const char * const delim = " "; // space character
    char *save = nullptr;
    const char * const cmd = strtok_r(msg_dup, delim, &save); // 1st arg

    if (strncmp(cmd, CHILD_PID_NOTIFY_CMD.c_str(), CHILD_PID_NOTIFY_CMD.size()) == 0) {
      // notify supervisor of a child process that was forked
      const char * const pid = strtok_r(nullptr, delim, &save);
      assert(pid != nullptr);
      const char * const uds_socket_name = strtok_r(nullptr, delim, &save);
      assert(uds_socket_name != nullptr);
      const char * cmd_line = uds_socket_name + (strlen(uds_socket_name) + sizeof('\0'));
      if (cmd_line == nullptr) {
        cmd_line = "";
      }
      if (!shm_session.spartanChildNotifyEntryPoint.empty()) {
        log(LL::DEBUG, "%s(): %s pid:%s '%s'", func_name, cmd, pid, cmd_line );
        const auto ec = invoke_java_child_processor_notify( pid, cmd_line, shm_session.jvm_sp.get(),
                                                            shm_session.spartanChildNotifyEntryPoint);
        if (ec != EXIT_SUCCESS) {
          log(LL::ERR, "invoke_java_child_processor_notify() did not complete successfully");
        }
      } else {
        log(LL::WARN, "%s(): no Java method defined to handle command:\n\t%s %s '%s'",
            func_name, cmd, pid, cmd_line);
      }
    } else if (strncmp(cmd, CHILD_PID_COMPLETION_NOTIFY_CMD.c_str(), CHILD_PID_COMPLETION_NOTIFY_CMD.size()) == 0) {
      const char * const pid = strtok_r(nullptr, delim, &save);
      assert(pid != nullptr);
      if (!shm_session.spartanChildCompletionNotifyEntryPoint.empty()) {
        // notify supervisor of a child process that has completed or exited
        log(LL::DEBUG, "%s(): %s pid:%s", func_name, cmd, pid);
        auto ec = invoke_java_child_processor_completion_notify(pid, shm_session.jvm_sp.get(),
                                                                shm_session.spartanChildCompletionNotifyEntryPoint);
        if (ec != EXIT_SUCCESS) {
          log(LL::ERR, "invoke_java_child_processor_completion_notify() did not complete successfully");
        }
      } else {
        log(LL::WARN, "%s(): no Java method defined to handle command:\n\t%s %s", func_name, cmd, pid);
      }
    } else {
      log(LL::DEBUG, "%s(): '%s'", func_name, msg);
      strtok_r(nullptr, delim, &save);                          // 2nd arg - unix datagram name (skip it)
      const char *cmd_token = strtok_r(nullptr, delim, &save);  // 3rd arg - sub-command name
      std::string cmd_str{};
      if (cmd_token != nullptr) {
        cmd_str = cmd_token;
        cmd_str.erase(std::remove(cmd_str.begin(), cmd_str.end(), '"'), cmd_str.end());
        cmd_token = cmd_str.c_str();
      }
      auto const check_errcode = [cmd_token](int errcode) {
        if (errcode != EXIT_SUCCESS) {
          log(LL::ERR, "invoke_java_supervisor_command() did not complete command %s successfully", cmd_token);
        }
      };
      int ec;
      if (cmd_token != nullptr && shm_session.spSpartanSupervisorCommands) {
        log(LL::TRACE, "@@@@ pid(%d): use shm_session to invoke supervisor command: %s\n\tsupervisor cmd vec size: %lu",
               getpid(), cmd_str.c_str(), shm_session.spSpartanSupervisorCommands->size());
        for (auto &methDesc : *(shm_session.spSpartanSupervisorCommands)) {
          if (icompare(methDesc.cmd_str(), cmd_str)) {
            ec = invoke_java_supervisor_command(argc, argv, msg, shm_session.jvm_sp.get(), methDesc);
            check_errcode(ec);
            return;
          }
        }
      }
      auto &methDesc = shm_session.spartanSupervisorEntryPoint;
      if (!methDesc.empty()) {
        ec = invoke_java_supervisor_command(argc, argv, msg, shm_session.jvm_sp.get(), methDesc);
        check_errcode(ec);
      } else {
        log(LL::WARN, "%s(): no Java method defined to handle command line:\n\t'%s'", func_name, msg);
      }
    }
  };

  handle_dispatch_msg_t const handle_dispatch_msg = is_launcher_process ? handle_launcher_msg : handle_supervisor_msg;

  // lambda dedicated to processing the msg C++11 work queue; a popped msg will
  // be the command line args to a Java method invoked on a forked child process
  // or on supervisor (lambda is executed on a dedicated detached async thread)
  {
    std::promise<void> prom;
    auto sf = prom.get_future().share();

    std::thread de_queue_dispatch_msg_thrd { std::function<void()>([&,sf,child_process_max_count] {
      sf.wait(); // Waits on calling thread to send notification to proceed
      for(;;) {
        std::unique_ptr<std::string[]> msg_array_sp;
        volatile int de_queue_count = 0;
        {
          msg_array_sp.reset(nullptr);
          volatile int count_headroom = 0;
          std::unique_lock<std::timed_mutex> lk(qm, std::defer_lock);
          if (lk.try_lock_for(std::chrono::seconds(3))) {
            qcv.wait_for(lk, std::chrono::seconds(2), [&,child_process_max_count] {
              count_headroom = child_process_max_count - child_process_count.load();
              return qready.load() && count_headroom > 0;
            });
            qready.store(false);
            const int queue_count = (int) dispatch_msg_queue.size();
            de_queue_count = queue_count > count_headroom ? count_headroom : queue_count;
            if (de_queue_count > 0) {
              msg_array_sp.reset(new std::string[de_queue_count]);
              for(int i = 0; i < de_queue_count; i++) {
                msg_array_sp[i] = dispatch_msg_queue.front();
                dispatch_msg_queue.pop();
              }
              child_process_count += de_queue_count;
            }
          }
        }
        if (msg_array_sp && de_queue_count > 0) {
          for(int i = 0; i < de_queue_count; i++) {
            // flag when non-zero indicates was signaled to terminate
            if (!jvm_shutting_down) {
              handle_dispatch_msg(msg_array_sp[i].c_str());
            }
          }
        }
      }
    }) };

    de_queue_dispatch_msg_thrd.detach();

    // signals notification to async de_queue_dispatch_msg() lambda to proceed
    prom.set_value();
  }

  /* lambda that enqueues an mq popped message onto a C++11 work queue; a queued msg will be the */
  /* command line args to a Java method invoked on the supervisor or on a forked child process   */
  auto const en_queue_dispatch_msg = [&dispatch_msg_queue, &qready](const char* const msg) -> processor_result_t {
    std::unique_lock<std::timed_mutex> lk(qm, std::defer_lock);
    if (lk.try_lock_for(std::chrono::seconds(5))) {
      dispatch_msg_queue.emplace(std::string(msg));
      qready.store(true);
      qcv.notify_one();
      return processor_result_t(true, EXIT_SUCCESS); // continue processing mq messages
    }
    log(LL::ERR, "failed to acquire lock to enqueue mq msg '%s'", msg);
    return processor_result_t(false, EXIT_FAILURE); // cease processing mq messages and terminate program
  };

  using msg_dispatch_t = std::function<processor_result_t(char [], const int)>;

  // lambda (with closure) that processes an mq message for launcher process - typically a dispatch to a handler
  msg_dispatch_t const msg_dispatch_for_launcher = [&en_queue_dispatch_msg]
      (const char* const buffer, const int msg_size) -> processor_result_t
  {
    const char * const msg_dup = strndupa(buffer, (size_t) msg_size);

    if (strcmp(msg_dup, STOP_CMD.c_str()) == 0) {
      return processor_result_t(false, EXIT_SUCCESS); // initiate exiting activity of parent supervisor process
    }

    // All other mq messages to be processed on a
    // forked child process context dealt with here
    return en_queue_dispatch_msg(msg_dup);
  };

  // lambda (with closure) that processes an mq message for supervisor process - typically a dispatch to a handler
  msg_dispatch_t const msg_dispatch_for_supervisor = [&en_queue_dispatch_msg, &shm_session]
      (const char* const buffer, const int msg_size) -> processor_result_t
  {
    static const char func_name[] = "msg_dispatch_for_supervisor";
    const char * const msg_dup = strndupa(buffer, (size_t) msg_size);

    if (strcmp(msg_dup, SHUTDOWN_CMD.c_str()) == 0) {
      jvm_shutting_down = true;
      const auto ec = invoke_java_method(shm_session.jvm_sp.get(), shm_session.spartanSupervisorShutdownEntryPoint);
      if (ec != EXIT_SUCCESS) {
        log(LL::ERR, "%s() did not complete command %s successfully", func_name, msg_dup);
      }
      return processor_result_t(false, ec); // complete exiting activity of parent supervisor process
    }
    if (strncmp(msg_dup, STATUS_CMD.c_str(), STATUS_CMD.size()) == 0) {
      if (!(flag != 0 || shutting_down || jvm_shutting_down)) {
        log(LL::INFO, "received: \"%s\"", msg_dup);
        static const char *const delim = " ";
        char *save = nullptr;
        strtok_r(const_cast<char *>(msg_dup), delim, &save);
        const char *const uds_socket_name(strtok_r(nullptr, delim, &save));
        std::packaged_task<decltype(supervisor_status_response)> async_get_status{supervisor_status_response};
        std::thread thrd{std::move(async_get_status), std::string(uds_socket_name), shm_session.jvm_sp.get(),
                         shm_session.spartanGetStatusEntryPoint};
        thrd.detach(); // launch on a thread
      }
      return processor_result_t(true, EXIT_SUCCESS); // continue processing mq messages
    }

    // All other mq messages to be processed on the
    // supervisor process context dealt with here
    return en_queue_dispatch_msg(msg_dup);
  };

  msg_dispatch_t const msg_dispatch = is_launcher_process ? msg_dispatch_for_launcher : msg_dispatch_for_supervisor;

  char buffer[MSG_BUF_SZ]; // buffer for received message from mq queue
  const int timeout_interval = 5; // seconds
  timespec timeout{};
  clock_gettime(CLOCK_REALTIME, &timeout);
  timeout.tv_sec += timeout_interval;

  // enter loop to read messages from mq queue; calls msg_dispatch() to deal with them
  bool loop_continue = true;
  do {
    unsigned msg_prio = 0;
    const int msg_sz = (int) mq_timedreceive(mqd_sp->_mqd, buffer, sizeof(buffer), &msg_prio, &timeout);
    const int ern = msg_sz < 0 ? errno : 0;
    if (ern == ETIMEDOUT) {
      if (flag != 0) {// check to see if signaled to terminate
        break;
      }
      clock_gettime(CLOCK_REALTIME, &timeout);
      timeout.tv_sec += timeout_interval;
      continue;
    }
    if (ern != 0) {
      log(LL::ERR, "mq_receive returned error: %s", strerror(ern));
      mqd_sp.reset(nullptr);
      auto prcsr_rslt = processor_result_t(false, EXIT_FAILURE);
      loop_continue = std::get<0>(prcsr_rslt);
      exit_code = std::get<1>(prcsr_rslt);
    } else if (msg_sz == 0 && flag == 0) { // also confirms flag is not indicating signal to terminate
      continue;
    }
    log(LL::DEBUG, "message size(%d) received", msg_sz);
    auto dispatch_rslt = msg_dispatch(buffer, msg_sz);
    log(LL::DEBUG, "returned from message dispatching of message size(%d)", msg_sz);
    loop_continue = std::get<0>(dispatch_rslt);
    exit_code = std::get<1>(dispatch_rslt);
  } while(loop_continue && flag == 0); // also confirms flag is not indicating signal to terminate

  return exit_code;
}

static void supervisor_child_processor_notify(const pid_t child_pid, const char * const command_line) {
  log(LL::TRACE, "forked child process %d for command line:\n\t'%s'", child_pid, command_line);
  int strbuf_size = 256;
  char *strbuf = (char*) alloca(strbuf_size);
  int n = strbuf_size;
  do_str_fmt: {
    n = snprintf(strbuf, (size_t) n, "%s %d %s", CHILD_PID_NOTIFY_CMD.c_str(), child_pid, command_line);
    assert(n > 0);
    if (n >= strbuf_size) {
      strbuf = (char*) alloca(strbuf_size = ++n);
      goto do_str_fmt; // try again
    }    
  }
  send_supervisor_mq_msg(strbuf);
}

static void supervisor_child_processor_completion_notify(const siginfo_t &info) {
  if (is_trace_level()) {
    switch (info.si_code) {
      case CLD_EXITED:
        log(LL::TRACE, "child process %d returned exit code %d", info.si_pid, info.si_status);
        break;
      case CLD_KILLED:
      case CLD_STOPPED:
        log(LL::TRACE, "child process %d terminated by signal", info.si_pid);
        break;
      default:
        log(LL::TRACE, "child process %d did not terminate normally", info.si_pid);
    }
  }
  int strbuf_size = 64;
  char *strbuf = (char*) alloca(strbuf_size);
  int n = strbuf_size;
  do_str_fmt: {
    n = snprintf(strbuf, (size_t) n, "%s %d", CHILD_PID_COMPLETION_NOTIFY_CMD.c_str(), info.si_pid);
    assert(n > 0);
    if (n >= strbuf_size) {
      strbuf = (char*) alloca(strbuf_size = ++n);
      goto do_str_fmt; // try again
    }    
  }
  send_supervisor_mq_msg(strbuf);
}

static int invoke_java_child_processor_notify(const char *const child_pid, const char *const command_line,
                                              JavaVM *const jvmp, const methodDescriptor &method_descriptor)
{
  std::array<char*, 3> argv = { const_cast<char*>(child_pid), const_cast<char*>(command_line), nullptr };
  return invoke_java_method(jvmp, method_descriptor, argv.size() - 1, argv.data()); // argc, argv parameters
}

static int invoke_java_child_processor_completion_notify(const char *const child_pid, JavaVM *const jvmp,
                                                         const methodDescriptor &method_descriptor)
{
  std::array<char*, 2> argv = { const_cast<char*>(child_pid), nullptr };
  return invoke_java_method(jvmp, method_descriptor, argv.size() - 1, argv.data()); // argc, argv parameters
}

static int invoke_child_process_action(sessionState &session_mut, const char *jvm_override_optns,
                                       const action_cb_t &action)
{
  int exit_code = EXIT_FAILURE; // assume will exit the forked child process with failure exit code
  try {
    if (session_mut.jvm_sp) {
      // invoke the processing logic of the forked child process and return its exit code
      exit_code = action(session_mut, session_mut.jvm_sp.get());
    } else {
      session_mut.create_jvm(jvm_override_optns);
      // don't want a forked child process to cleanup the Java JVM
      auto const env = session_mut.env_sp.release();
      auto const jvm = session_mut.jvm_sp.release();
      jni_detach_thread(jvm, env);
      (void) session_mut.libjvm_sp.release();
      // invoke the processing logic of the forked child process and return its exit code
      exit_code = action(session_mut, jvm);
    }
  } catch (const spartan_exception &e) {
    log(LL::ERR, "child process %d terminating due to:\n\t%s: %s", getpid(), e.name(), e.what());
  } catch (const std::exception &e) {
    const auto ex_nm = get_unmangled_name(typeid(e).name());
    log(LL::ERR, "child process %d terminating due to:\n\t%s: %s", getpid(), ex_nm.c_str(), e.what());
  } catch (...) {
    const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
    log(LL::ERR, "child process %d terminating due to unhandled exception of type %s", getpid(), ex_nm.c_str());
  }
  return exit_code;
}

using raii_argv_sp_t = std::unique_ptr<const char*, std::function<void(const char**)>>;

static raii_argv_sp_t parse_cmd_line(const char *const cmd_line, const char *const desc, int &argc_cmd_line, int &rc) {
  static const char *const func_name = __FUNCTION__;
  rc = EXIT_SUCCESS;
  argc_cmd_line = 0;
  const char**argv_cmd_line = nullptr;
  auto rtn = poptParseArgvString(cmd_line, &argc_cmd_line, &argv_cmd_line);
  if (is_trace_level()) {
    log(LL::TRACE, "%s() %s %d rtn: %d, argc: %d", func_name, desc, getpid(), rtn, argc_cmd_line);
    if (rtn == 0) {
      for (int i = 0; i < argc_cmd_line; i++) {
        printf("\targv[%d]: %s\n", i, argv_cmd_line[i]);
      }
    }
  }
  if (rtn != 0) {
    const char* const errmsg = poptStrerror(rtn);
    log(LL::ERR, "%s() %s %d Failed parsing command line:\n\t%s", func_name, desc, getpid(), errmsg);
    rc = EXIT_FAILURE;
  }
  auto const curr_pid = getpid();
  return raii_argv_sp_t{ argv_cmd_line, [curr_pid](const char**p) {
    log(LL::TRACE, "%s() pid:%d, freeing argv storage of command line", func_name, curr_pid);
    std::free(p);
  }};
}

static bool parse_extended_invoke_option(string_view const extended_invoke_cmd) {
  if (strncmp(extended_invoke_cmd.c_str(), EXTENDED_INVOKE_CMD.c_str(), EXTENDED_INVOKE_CMD.size()) == 0) {
    auto const str = strndupa(extended_invoke_cmd.c_str(), extended_invoke_cmd.size());
    const auto delim = "=";
    char *saveptr = nullptr;
    strtok_r(str, delim, &saveptr); // skip 1st token
    auto const val = strtok_r(nullptr, delim, &saveptr);
    return strcmp(val, "true") == 0;
  }
  return false;
}

static int core_invoke_command(int /*argc*/, char **/*argv*/, const char *const msg_arg,
                               JavaVM *const jvmp, const methodDescriptor &method_descriptor,
                               const char *const func_name, const char *const desc)
{
  const auto pid = getpid();
  log(LL::INFO, "%s() %s %d processing:\n\t\'%s\'", func_name, desc, pid, msg_arg);

  int rc;
  int argc_cmd_line;

  auto raii_argv_sp = parse_cmd_line(msg_arg, desc, argc_cmd_line, rc);
  if (rc == EXIT_SUCCESS) {
    auto const argv_cmd_line = raii_argv_sp.get();
    if (argc_cmd_line < 3) {
      log(LL::ERR, "%s() %s %d unexpected error - invalid command line - insufficient arguments:\n\t'%s'",
          func_name, desc, getpid(), msg_arg);
      rc = EXIT_FAILURE;
    } else {
      auto const extd_invoke_cmd = argv_cmd_line[0]; // by convention first arg must be extended-invoke-command
      auto const uds_socket_name = argv_cmd_line[1]; // by convention second arg must be unix datagram name
      const auto is_extended_invoke = parse_extended_invoke_option(extd_invoke_cmd) ||
                                      react_invoke_descriptor.compare(method_descriptor.desc_str()) == 0;
      auto const no_op_cleanup = [](fd_wrapper_t *) {};
      std::array<fd_wrapper_sp_t, 3> fds_array {{
          {nullptr, no_op_cleanup}, {nullptr, no_op_cleanup}, {nullptr, no_op_cleanup} }};
      if (is_extended_invoke) {
        // obtain 3 fd (file descriptors) - the response stream, the error stream, and the input stream
        auto rslt = open_react_anon_pipes(uds_socket_name, rc);
        if (rc == EXIT_SUCCESS) {
          fds_array[0] = std::move(std::get<0>(rslt));
          fds_array[1] = std::move(std::get<1>(rslt));
          fds_array[2] = std::move(std::get<2>(rslt));
        }
      } else {
        // just need to obtain 1 fd (file descriptor) - the response stream
        auto fd_sp = open_write_anon_pipe(uds_socket_name, rc);
        if (rc == EXIT_SUCCESS) {
          fds_array[0] = std::move(fd_sp);
        }
      }
      if (rc == EXIT_SUCCESS) {
        rc = invoke_java_method(jvmp, method_descriptor, std::move(fds_array),
                                argc_cmd_line - 1, const_cast<char **>(&argv_cmd_line[1]));
      }
    }
  }

  log(LL::DEBUG, "%s() %s %d returning %s", func_name, desc, pid, rc == EXIT_SUCCESS ? "EXIT_SUCCESS" : "EXIT_FAILURE");
  return rc;
}

static int invoke_java_supervisor_command(int argc, char **argv, const char* const msg_arg, JavaVM* const jvmp,
                                          const methodDescriptor &method_descriptor)
{
  return core_invoke_command(argc, argv, msg_arg, jvmp, method_descriptor, __func__, "supervisor process");
}

// function where primary processing logic of forked child process initiates
static int invoke_child_processor_command(int argc, char **argv, const char* const msg_arg, JavaVM* const jvmp,
                                          const methodDescriptor &method_descriptor)
{
  return core_invoke_command(argc, argv, msg_arg, jvmp, method_descriptor, __func__, "child process");
}