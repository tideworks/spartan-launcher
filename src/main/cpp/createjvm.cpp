/* createjvm.cpp

Copyright 2015 - 2017 Tideworks Technology
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
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <unistd.h>
#include <popt.h>
#include "format2str.h"
#include "log.h"
#include "findfiles.h"
#include "createjvm.h"

using namespace logger;

const char PATH_SEPARATOR =
#ifdef _WIN32
                            ';';
#else
                            ':';
#endif
#define USER_CLASSPATH "."

using const_char_ptr_t = const char *;

static const_char_ptr_t const failed_setting_cwd_wrn_msg_fmt = "%s() unsuccessful setting as "
                                                               "current working directory:\n\t\"%s\"\n\t%s";
static const_char_ptr_t const jvm_classpath_optn_str = "-Djava.class.path=";
static const_char_ptr_t const set_cwd_optn = "-Duser.dir=";
static const unsigned int base_optns = 3;

extern const_char_ptr_t java_classpath();
extern const_char_ptr_t java_home_path();
extern const_char_ptr_t progname();
extern const_char_ptr_t jvm_cmd_line_args();
extern void set_exit_flag_true();

static std::string get_java_classpath() {
  std::string jvm_classpath_optn(jvm_classpath_optn_str);
  const std::string user_classpath(USER_CLASSPATH);
  std::string classpath(java_classpath());
  const auto is_classpath_empty = classpath.empty() || classpath[0] == '\0';
  const auto is_user_classpath_empty = user_classpath.empty() || user_classpath[0] == '\0';
  if (!is_classpath_empty && !is_user_classpath_empty && classpath[0] != user_classpath[0])
  {
    classpath = user_classpath + PATH_SEPARATOR + classpath;
  }
  return jvm_classpath_optn + (is_classpath_empty ? user_classpath : classpath);
}

// Determine the Java JVM runtime library path via JAVA_HOME environment variable
// (returns just the jvm library file name if fails to locate it under JAVA_HOME)
std::string determine_jvmlib_path() {
  log(LL::DEBUG, "Java environment variables:\n\tJAVA $JAVA_HOME=%s\n\tJAVA $CLASSPATH=%s",
      java_home_path(), java_classpath());

  const_char_ptr_t const jvmlib_name = "libjvm.so";
  std::string jvmlib_path(jvmlib_name);
  try {
    if (!findfiles(java_home_path(), [jvmlib_name,&jvmlib_path](const_char_ptr_t const filepath,
                                                                const_char_ptr_t const filename)
    {
      if (strcasecmp(filename, jvmlib_name) == 0) {
        jvmlib_path = std::string(filepath);
        return true;
      }
      return false;
    })) {
      log(LL::ERR, "failed to find Java JVM runtime \"%s\"", jvmlib_name);
    } else {
      log(LL::DEBUG, "using Java JVM runtime located at:\n\t\"%s\"", jvmlib_path.c_str());
    }
  } catch(findfiles_exception& ex) {
    log(LL::ERR, "failed to find Java JVM runtime \"%s\"\n\t%s: %s", jvmlib_name, ex.name(), ex.what());
  }
  return jvmlib_path;
}

void* open_jvm_runtime_module(const_char_ptr_t const jvmlib_path) {
  // load the Java JVM runtime module
  void * const libjvm_hnd = dlopen(jvmlib_path, RTLD_LAZY);
  if (libjvm_hnd == nullptr) {
    static const_char_ptr_t const err_msg_fmt = "failed to load the Java JVM runtime \"%s\"\n\t%s";
    auto errmsg( format2str(err_msg_fmt, jvmlib_path, dlerror()) );
    throw create_jvm_exception(std::move(errmsg));
  }
  return libjvm_hnd;
}

static void JNICALL jvm_exiting(jint /*code*/) {
  set_exit_flag_true();
}

static void JNICALL jvm_aborting() {
  set_exit_flag_true();
}

static jint set_JavaVMOptions(unsigned int argc, const char**argv, std::string optionStrs[], JavaVMOption options[]) {
  jint n = base_optns;

  // designate a CLASSPATH for the JVM
  auto &first_option = options[0];
  auto &first_option_str = optionStrs[0];
  first_option_str = get_java_classpath();
  first_option.optionString = const_cast<char*>(first_option_str.c_str());
  first_option.extraInfo = nullptr;

  // install an on-exit callback for when JVM exits on its own
  auto &second_option = options[1];
  auto &second_option_str = optionStrs[1];
  second_option_str = std::string("exit");
  second_option.optionString = const_cast<char*>(second_option_str.c_str());
  second_option.extraInfo = reinterpret_cast<void*>(jvm_exiting);

  // install an on-abort callback for when JVM aborts on its own
  auto &third_option = options[2];
  auto &third_option_str = optionStrs[2];
  third_option_str = std::string("abort");
  third_option.optionString = const_cast<char*>(third_option_str.c_str());
  third_option.extraInfo = reinterpret_cast<void*>(jvm_aborting);

  if (argc > 0) {
    const auto jvm_clspath_optn_len = strlen(jvm_classpath_optn_str);
    const auto set_cwd_optn_len = strlen(set_cwd_optn);
    for(unsigned int i = 0, j = 0; i < argc; i++) {
      const_char_ptr_t const curr_arg = argv[i];
      auto &option = options[j + base_optns];
      option.optionString = nullptr;
      option.extraInfo = nullptr;
      // check for JVM CLASSPATH option (overrides default and is passed on to JVM initialization)
      if (strncmp(curr_arg, jvm_classpath_optn_str, jvm_clspath_optn_len) == 0) {
        first_option.optionString = nullptr;
        first_option.extraInfo = nullptr;
        first_option_str = curr_arg;
        first_option.optionString = const_cast<char*>(first_option_str.c_str());
      }
      // check for set-current-working-directory option
      else if (strncmp(curr_arg, set_cwd_optn, set_cwd_optn_len) == 0) {
        const auto len = strlen(curr_arg);
        if (len > set_cwd_optn_len) {
          auto const cwd = &curr_arg[set_cwd_optn_len];
          // change current working directory to the one specified
          // (do this here as JVM has no option to do this itself)
          const auto rtn = chdir(cwd);
          if (rtn != 0) {
            log(LL::ERR, failed_setting_cwd_wrn_msg_fmt, __func__, curr_arg, strerror(errno));
          }
        }
      }
      // pass on as a JVM initialization argument
      else {
        auto &optionStr = optionStrs[j++ + base_optns];
        optionStr = curr_arg;
        option.optionString = const_cast<char*>(optionStr.c_str());
        n++;
      }
    }
  }
  return n;
}

static bool consolidate_jvm_options(int &argc, const_char_ptr_t argv[]) {
  if (argc < 2) return false;
  static const char char_set[] = "=:0123456789";
  std::unordered_set<std::string> jvm_options_prefixes(29);
  int removed_count = 0;
  for(int i = 0; i < argc; i++) {
    std::string arg(argv[i]);
    auto found_pos = arg.find_first_of(char_set);
    if (found_pos != std::string::npos) {
      arg = arg.substr(0, found_pos);
    }
    if (jvm_options_prefixes.count(arg) > 0) {
      argv[i] = nullptr;
      removed_count++;
    } else {
      jvm_options_prefixes.emplace(std::move(arg));
    }
  }
  if (removed_count > 0) {
    // compact the argv array (nullptr entries are moved to the end of the array)
    int swap_count = 0;
    for(int i = 0, j = 1; i < argc - 1 && j < argc; ) {
      auto &curr = argv[i];
      auto &next = argv[j];
      if (curr == nullptr) {
        if (next != nullptr) {
          curr = next;
          next = nullptr;
          swap_count++;
        } else {
          j++;
          continue;
        }
      }
      i++;
      j++;
    }
    log(LL::TRACE, "%s(argc:%d): removed_count: %d, swap_count: %d", __func__ , argc, removed_count, swap_count);
    argc -= removed_count;
    return true;
  }
  return false;
}

// Instantiates the Java JVM runtime - returns a tuple type pair of JavaVM* and JNIEnv*
// (throws exception if fails to instantiate JVM)
jvm_create_t create_jvm(void * const hlibjvm, const_char_ptr_t jvm_override_optns) {
  static auto const func_name = __func__;

  const std::string cmd_line_args = [](const_char_ptr_t jvm_override_optns_param) -> std::string {
    std::stringstream ss;
    if (jvm_override_optns_param != nullptr && jvm_override_optns_param[0] != 0) {
      ss << jvm_override_optns_param << ' ';
    }
    ss << jvm_cmd_line_args();
    return ss.str();
  }(jvm_override_optns);

  int argc = 0;
  const_char_ptr_t *argv = nullptr;

  const auto rtn = poptParseArgvString(cmd_line_args.c_str(), &argc, &argv);
  auto const log_print_argv = [rtn](const_char_ptr_t desc, int _argc, const_char_ptr_t _argv[]) {
    if (is_trace_level()) {
      log(LL::TRACE, "%s::log_print_argv() %s of jvm_cmd_line_args: rtn: %d, argc: %d", func_name, desc, rtn, _argc);
      if (rtn == 0) {
        for (int i = 0; i < _argc; i++) {
          printf("\targv[%d]: %s\n", i, _argv[i]);
        }
      }
    }
  };
  log_print_argv("parse", argc, argv);
  if (rtn != 0) {
    static const_char_ptr_t const err_msg_fmt = "%s() failed parsing Java JVM command line:\n\t%s";
    auto errmsg( format2str(err_msg_fmt, func_name, poptStrerror(rtn)) );
    throw create_jvm_exception(std::move(errmsg));
  }
  auto const cleanup = [](const_char_ptr_t *p) {
    log(LL::TRACE, "%s::cleanup(%p) freeing Java jvm argv storage of command line", func_name, p);
    if (p != nullptr) {
      std::free(p);
    }
  };
  std::unique_ptr<const_char_ptr_t, decltype(cleanup)> raii_argv_sp(argv, cleanup);

  if (argc > 1 && consolidate_jvm_options(argc, argv)) {
    log_print_argv("merge", argc, argv);
  } else if (argc <= 0) {
    argv = nullptr;
  }
  jint count = argc + base_optns;

  std::unique_ptr<std::string[]> optionStrsSP(new std::string[count]);
  auto const jvm_args_options = (JavaVMOption*) alloca(sizeof(JavaVMOption) * count);
  // initialize stack allocated array of JavaVMOption
  for(int i = 0; i < count; i++) {
    auto &option = jvm_args_options[i];
    option.optionString = nullptr;
    option.extraInfo = nullptr;
  }
  // now populate with actual arguments to be passed to JVM
  count = set_JavaVMOptions(static_cast<unsigned>(argc), argv, optionStrsSP.get(), jvm_args_options);

  JavaVMInitArgs vm_args = {0};
  vm_args.version = JNI_VERSION_1_6; //JDK version. This indicates version 1.6
  vm_args.options = jvm_args_options;
  vm_args.nOptions = count;
  vm_args.ignoreUnrecognized = JNI_TRUE;

  const_char_ptr_t const create_jvm_func_name = "JNI_CreateJavaVM";
  typedef jint (JNICALL *JNI_CreateJavaVM_Proc_t)(JavaVM **/*pvm*/, void **/*penv*/, void */*args*/);
  auto const JNI_CreateJavaVM_proc = (JNI_CreateJavaVM_Proc_t) dlsym(hlibjvm, create_jvm_func_name);
  if (is_trace_level()) {
    log(LL::TRACE, "%s() Java JVM args: %d", func_name, count);
    for(int i = 0; i < count; i++) {
      auto &option = jvm_args_options[i];
      printf("\t%s\n", option.optionString);
    }
  }
  /* Create the Java VM */
  JavaVM *jvmp = nullptr;
  JNIEnv* envp = nullptr;
  const auto res = JNI_CreateJavaVM_proc(&jvmp, (void**)&envp, &vm_args);
  if (res < 0) {
    static const_char_ptr_t const err_msg_fmt = "failed to obtain function %s() for creating JVM instance";
    auto errmsg( format2str(err_msg_fmt, create_jvm_func_name) );
    throw create_jvm_exception(std::move(errmsg));
  }

  return jvm_create_t(jvmp, envp);
}

