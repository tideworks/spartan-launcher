/* session-state.cpp

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
#include <cstring>
#include <alloca.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <stdexcept>
#include <typeinfo>
#include <algorithm>
#include <popt.h>
#include <fstream>
#include "string-view.h"
#include "log.h"
#include "format2str.h"
#include "cfgparse.h"
#include "createjvm.h"
#include "launch-program.h"
#include "session-state.h"
#include "findfiles.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

using namespace logger;

using launch_program::try_resolve_program_path;

using bpstd::string_view;

const char PATH_SEPARATOR =
#ifdef _WIN32
                            ';';
#else
                            ':';
#endif

// these externs are defined in spartan.cpp and are initialized in main() entry point
extern const char* executable_dir();
extern const char* progpath();
extern const char* progname();

// will be populated with a value when config.ini file is parsed
static string_view s_jvm_cmd_line_args{};
const char* jvm_cmd_line_args() { return s_jvm_cmd_line_args.c_str(); }

static const string_view boot_classpath_optn_strv{ "-Xbootclasspath/a:" };
static const string_view java_lib_path_optn_strv { "-Djava.library.path=" };
static const string_view spartan_strv{ "Spartan" };
static const string_view jar_file_extent_strv{ ".jar" };


void sessionState::close_libjvm(void *hlibjvm) {
  const auto pid = getpid();
  log(LL::TRACE, ">> %s(hlibjvm) - pid(%d)", __func__, pid);
  if (hlibjvm != nullptr) {
    log(LL::TRACE, "about to close the loaded Java JVM runtime module - pid(%d)", pid);
    dlclose(hlibjvm);
    log(LL::DEBUG, "closed the loaded Java JVM runtime module - pid(%d)", pid);
  }
}

void sessionState::cleanup_jvm(JavaVM *jvmp) {
  const auto pid = getpid();
  log(LL::TRACE, ">> %s(jvmp) - pid(%d)", __func__, pid);
  if (jvmp != nullptr) {
    log(LL::TRACE, "about to destroy the Java JVM runtime instance - pid(%d)", pid);
    jvmp->DestroyJavaVM();
    log(LL::DEBUG, "destroyed the Java JVM runtime instance - pid(%d)", pid);
 }
}

void sessionState::cleanup_jnienv(JNIEnv *envp) {
  const auto pid = getpid();
  log(LL::TRACE, ">> %s(envp) - pid(%d)", __func__, pid);
  if (envp != nullptr) {
    log(LL::TRACE, "check if any Java JVM exceptions to describe - pid(%d)", pid);
    if (envp->ExceptionOccurred() != nullptr) {
      log(LL::TRACE, "about to describe Java JVM exceptions - pid(%d)", pid);
      envp->ExceptionDescribe();
      log(LL::DEBUG, "described Java JVM exceptions - pid(%d)", pid);
    }
  }
}

methodDescriptor & methodDescriptor::operator=(methodDescriptor && md) noexcept {
  fullMethodName = std::move(md.fullMethodName);
  descriptor = std::move(md.descriptor);
  isStaticMethod = md.isStaticMethod;
  whichMethod = md.whichMethod;
  return *this;
}

methodDescriptorCmd & methodDescriptorCmd::operator=(methodDescriptorCmd && md) noexcept {
  methodDescriptor::operator=(std::move(md));
  command = std::move(md.command);
  jvmOptionsCommandLine = std::move(md.jvmOptionsCommandLine);
  return *this;
}

methodDescriptor & methodDescriptor::operator=(const methodDescriptor & md) {
  fullMethodName = md.fullMethodName;
  descriptor = md.descriptor;
  isStaticMethod = md.isStaticMethod;
  whichMethod = md.whichMethod;
  return *this;
}

methodDescriptorCmd & methodDescriptorCmd::operator=(const methodDescriptorCmd & md) {
  methodDescriptor::operator=(md);
  command = md.command;
  jvmOptionsCommandLine = md.jvmOptionsCommandLine;
  return *this;
}

std::ostream& operator << (std::ostream &os, const methodDescriptor &self) {
  using self_t = std::remove_reference<decltype(self)>::type;
  os << typeid(self_t).name() << '\n';
  os << self.isStaticMethod << '\n';
  os << static_cast<short int>(self.whichMethod) << '\n';
  os << self.fullMethodName << '\n';
  os << self.descriptor << '\n';
  return os;
}

std::ostream& operator << (std::ostream &os, const methodDescriptorCmd &self) {
  using self_t = std::remove_reference<decltype(self)>::type;
  os << typeid(self_t).name() << '\n';
  os << static_cast<const methodDescriptor&>(self);
  os << self.command << '\n';
  os << self.jvmOptionsCommandLine << '\n';
  return os;
}

std::istream& operator >> (std::istream &is, methodDescriptor &self) {
  std::string type_name;
  std::getline(is, type_name, '\n');
  is >> self.isStaticMethod;
  char newline;
  is.getline(&newline, 1);
  short int sint;
  is >> sint;
  self.whichMethod = static_cast<WhichMethod>(sint);
  is.getline(&newline, 1);
  std::getline(is, self.fullMethodName, '\n');
  std::getline(is, self.descriptor, '\n');
  return is;
}

std::istream& operator >> (std::istream &is, methodDescriptorCmd &self) {
  std::string type_name;
  std::getline(is, type_name, '\n');
  is >> static_cast<methodDescriptor&>(self);
  std::getline(is, self.command, '\n');
  std::getline(is, self.jvmOptionsCommandLine, '\n');
  return is;
}

// determine which directory to look for the config file:
// if program path is a symbolic link then look in the directory of the symbolic
// link file; otherwise look in the directory of the actual Spartan executable
std::string get_cfg_dir(const char * const cfg_file) {
  const char * cfg_dir = executable_dir();
  std::string progfullpath(progpath());
  struct stat statbuf{};
  auto rc = lstat(progfullpath.c_str(), &statbuf);
  if (rc == -1) {
    auto rslt = try_resolve_program_path(progpath(), "PATH");
    if (std::get<1>(rslt)) {
      progfullpath = std::get<0>(rslt);
      rc = lstat(progfullpath.c_str(), &statbuf);
      if (rc == -1) {
        log(LL::WARN, "%s(): stat(\"%s\") failed - %s", __func__, progfullpath.c_str(), strerror(errno));
      }
    }
  }
  if (rc != -1 && S_ISLNK(statbuf.st_mode)) {
    const auto progdirpath = [](const char * const path) -> std::string {
      auto const dup_path = strdupa(path);
      return std::string(dirname(dup_path));
    }(progfullpath.c_str());
    auto cfg_path = progdirpath;
    cfg_path += '/';
    cfg_path += cfg_file;
    if (stat(cfg_path.c_str(), &statbuf) == 0 && (statbuf.st_mode & S_IFMT) == S_IFREG) {
      cfg_dir = progdirpath.c_str();
    }
  }
  return std::string(cfg_dir);
}

static string_view prepend_to_java_library_path(const string_view jvm_cmd_line_args); // forward declaration

#ifdef _DEBUG
void debug_dump_sessionState(const sessionState& ss, const char edition) {
  std::string filename_01("sessionState-01X.ser");
  std::replace(filename_01.begin(), filename_01.end(), 'X', edition);
  std::ofstream ofs(filename_01, std::ios::trunc);
  if (ofs.is_open()) {
    std::ostream os1(ofs.rdbuf());
    os1 << ss;
    ofs.close();
    sessionState tmp;
    {
      std::ifstream ifs(filename_01);
      if (ifs.is_open()) {
        std::istream& is = ifs;
        is >> tmp;
        ifs.close();
      }
    }
    std::string filename_02("sessionState-02X.ser");
    std::replace(filename_02.begin(), filename_02.end(), 'X', edition);
    ofs.open(filename_02, std::ios::trunc);
    if (ofs.is_open()) {
      std::ostream os2(ofs.rdbuf());
      os2 << tmp;
      ofs.close();
    }
  }
}
#endif

enum class WhichInitError : int { UNKNOWN, MISSING_CFG, CFG_PARSING_ERR, MISSING_COMMANDS };
using WIE = WhichInitError;

// constructor that initializes the sessionState data structure
sessionState::sessionState(const char * const cfg_file, const char * const jvmlib_path) : jvmlib_path(jvmlib_path) {

  const char *caught_ex_name = "";
  const char *caught_ex_what = "";
  const char *missing_item = "";

  auto const raise_initialization_exception = [&, cfg_file](const WhichInitError which) {
    std::string err_msg;
    switch (which) {
      case WIE::MISSING_CFG:
        err_msg = format2str("\"%s\" not found", cfg_file);
        break;
      case WIE::CFG_PARSING_ERR:
        err_msg = format2str("failure attempting to process \"%s\":\n\t%s: %s",
                             cfg_file, caught_ex_name, caught_ex_what);
        break;
      case WIE::MISSING_COMMANDS:
        err_msg = format2str(R"("%s" missing required setting "%s")", cfg_file, missing_item);
        break;
      default:
        err_msg = format2str("unspecified exception processing \"%s\"", cfg_file);
    }
    throw invalid_initialization_exception(std::move(err_msg));
  };

  // if there is a config file, get settings from it
  try {
    const std::string cfg_dir( get_cfg_dir(cfg_file) );

    if (!process_config(cfg_dir.c_str(), cfg_file, [&](const char *section, const char *name, const char *value_cstr) {
      std::string value, descriptor;
      if (strcasecmp(section, "JvmSettings") == 0) {
        if (strcasecmp(name, "CommandLineArgs") == 0) {
          s_jvm_cmd_line_args = prepend_to_java_library_path(value_cstr);
        }
      } else if (strcasecmp(section, "SupervisorProcessSettings") == 0) {
        if (strcasecmp(name, "MainEntryPoint") == 0) {
          value = value_cstr;
          if (!value.empty()) {
            std::replace(value.begin(), value.end(), '.', '/');
            value += "/main";
            descriptor = "([Ljava/lang/String;)V";
            methodDescriptor method_descriptor(std::move(value), std::move(descriptor), true, WM::MAIN);
            spartanMainEntryPoint = std::move(method_descriptor);
          }
        } else if (strcasecmp(name, "GetStatusEntryPoint") == 0) {
          value = value_cstr;
          if (!value.empty()) {
            std::replace(value.begin(), value.end(), '.', '/');
            descriptor = "(Ljava/io/PrintStream;)V";
            methodDescriptor method_descriptor(std::move(value), std::move(descriptor), false, WM::GET_STATUS);
            spartanGetStatusEntryPoint = std::move(method_descriptor);
          }
        } else if (strcasecmp(name, "SupervisorShutdownEntryPoint") == 0) {
          value = value_cstr;
          if (!value.empty()) {
            std::replace(value.begin(), value.end(), '.', '/');
            descriptor = "()V";
            methodDescriptor method_descriptor(std::move(value), std::move(descriptor), false, WM::SUPERVISOR_SHUTDOWN);
            spartanSupervisorShutdownEntryPoint = std::move(method_descriptor);
          }
        } else if (strcasecmp(name, "ChildNotifyEntryPoint") == 0) {
          value = value_cstr;
          if (!value.empty()) {
            std::replace(value.begin(), value.end(), '.', '/');
            descriptor = "(ILjava/lang/String;)V";
            methodDescriptor method_descriptor(std::move(value), std::move(descriptor), false, WM::CHILD_NOTIFY);
            spartanChildNotifyEntryPoint = std::move(method_descriptor);
          }
        } else if (strcasecmp(name, "ChildCompletionNotifyEntryPoint") == 0) {
          value = value_cstr;
          if (!value.empty()) {
            std::replace(value.begin(), value.end(), '.', '/');
            descriptor = "(I)V";
            methodDescriptor method_descriptor(std::move(value), std::move(descriptor),
                                               false, WM::CHILD_COMPLETION_NOTIFY);
            spartanChildCompletionNotifyEntryPoint = std::move(method_descriptor);
          }
        } else if (strcasecmp(name, "SupervisorEntryPoint") == 0) {
          if (!value.empty()) {
            value = value_cstr;
            std::replace(value.begin(), value.end(), '.', '/');
            descriptor = "([Ljava/lang/String;Ljava/io/PrintStream;)V";
            methodDescriptor method_descriptor(std::move(value), std::move(descriptor), false, WM::SUPERVISOR_DO_CMD);
            spartanSupervisorEntryPoint = std::move(method_descriptor);
          }
        }
      } else if (strcasecmp(section, "ChildProcessSettings") == 0) {
        if (strcasecmp(name, "ChildProcessMaxCount") == 0) {
          auto const handle_exception = [name](const char * const e_what, const int default_value) {
            log(LL::WARN, "invalid value for setting %s - %s\n\tdefaulting to %d", name, e_what, default_value);
          };
          child_process_max_count = 40; // default value
          try {
            value = value_cstr;
            child_process_max_count = static_cast<short int>(std::stoi(value));
          } catch(const std::invalid_argument& e) {
            handle_exception(e.what(), child_process_max_count);
          } catch(const std::out_of_range& e) {
            handle_exception(e.what(), child_process_max_count);
          }
        } else if (strcasecmp(name, "ChildProcessorEntryPoint") == 0) {
          value = value_cstr;
          if (!value.empty()) {
            std::replace(value.begin(), value.end(), '.', '/');
            descriptor = "([Ljava/lang/String;Ljava/io/PrintStream;)V";
            methodDescriptor method_descriptor(std::move(value), std::move(descriptor), true, WM::CHILD_DO_CMD);
            spartanChildProcessorEntryPoint = std::move(method_descriptor);
          }
        } else if (strcasecmp(name, "ChildProcessorCommands") == 0) {
          value = value_cstr;
          spartanChildProcessorCommands = std::move(value);
        }
      } else if (strcasecmp(section, "LoggingSettings") == 0) {
        if (strcasecmp(name, "LoggingLevel") == 0) {
          const auto logging_level = logger::str_to_level(value_cstr);
          logger::set_level(logging_level);
          value = value_cstr;
          spartanLoggingLevel = std::move(value);
        }
      }
      return 1;
    })) {
      raise_initialization_exception(WIE::MISSING_CFG);
    }
  } catch(const process_cfg_exception& ex) {
    caught_ex_name = ex.name();
    caught_ex_what = ex.what();
    raise_initialization_exception(WIE::CFG_PARSING_ERR);
  } catch(const invalid_initialization_exception& ex) {
    throw;
  } catch(...) {
    raise_initialization_exception(WIE::UNKNOWN);
  }

  if (s_jvm_cmd_line_args.empty()) {
    s_jvm_cmd_line_args = prepend_to_java_library_path(string_view{});
  }

  std::string class_name;

  // do checking for required Spartan entry point methods
  [&class_name, this]() {
    if (!spartanMainEntryPoint.empty()) {
      // extract the class name from the fully qualified main entry point method name
      class_name = [](const char *const full_entry_point_name) -> std::string {
        const char *class_name_cstr = strdupa(full_entry_point_name);
        auto str = const_cast<char*>(strrchr(class_name_cstr, '/'));
        if (str == nullptr || *(str + 1) == '\0') return std::string();
        *str = '\0';  // null terminate the class name string (now refers to just the class)
        return std::string(class_name_cstr);
      }(spartanMainEntryPoint.c_str());
    }

    if (class_name.empty()) {
      class_name = "spartan/SpartanBase"; // if main entry point class not provided, then default to class SpartanBase
    } else {
      std::replace(class_name.begin(), class_name.end(), '.', '/');
    }

    std::string method_name, descriptor;

    if (spartanGetStatusEntryPoint.empty()) {
      method_name = class_name + "/status";
      descriptor = "(Ljava/io/PrintStream;)V";
      methodDescriptor method_descriptor(std::move(method_name), std::move(descriptor), false, WM::GET_STATUS);
      spartanGetStatusEntryPoint = std::move(method_descriptor);
    }
    if (spartanSupervisorShutdownEntryPoint.empty()) {
      method_name = class_name + "/supervisorShutdown";
      descriptor = "()V";
      methodDescriptor method_descriptor(std::move(method_name), std::move(descriptor), false, WM::SUPERVISOR_SHUTDOWN);
      spartanSupervisorShutdownEntryPoint = std::move(method_descriptor);
    }
    if (spartanChildNotifyEntryPoint.empty()) {
      method_name = class_name + "/childProcessNotify";
      descriptor = "(ILjava/lang/String;)V";
      methodDescriptor method_descriptor(std::move(method_name), std::move(descriptor), false, WM::CHILD_NOTIFY);
      spartanChildNotifyEntryPoint = std::move(method_descriptor);
    }
    if (spartanChildCompletionNotifyEntryPoint.empty()) {
      method_name = class_name + "/childProcessCompletionNotify";
      descriptor = "(I)V";
      methodDescriptor method_descriptor(std::move(method_name), std::move(descriptor),
                                         false, WM::CHILD_COMPLETION_NOTIFY);
      spartanChildCompletionNotifyEntryPoint = std::move(method_descriptor);
    }
    if (spartanSupervisorEntryPoint.empty()) {
      method_name = class_name + "/supervisorDoCommand";
      descriptor = "([Ljava/lang/String;Ljava/io/PrintStream;)V";
      methodDescriptor method_descriptor(std::move(method_name), std::move(descriptor), false, WM::SUPERVISOR_DO_CMD);
      spartanSupervisorEntryPoint = std::move(method_descriptor);
    }
  }();

  if (!spartanChildProcessorEntryPoint.empty() && spartanChildProcessorCommands.empty()) {
    missing_item = "ChildProcessorCommands";
    raise_initialization_exception(WIE::MISSING_COMMANDS);
  } else if (spartanChildProcessorEntryPoint.empty()) {
    std::string method_name = class_name + "/childWorkerDoCommand";
    std::string descriptor = "([Ljava/lang/String;Ljava/io/PrintStream;)V";
    methodDescriptor method_descriptor(std::move(method_name), std::move(descriptor), true, WM::CHILD_DO_CMD);
    spartanChildProcessorEntryPoint = std::move(method_descriptor);
  }

  // load and open the JVM runtime module
  libjvm_sp.reset(open_jvm_runtime_module(this->jvmlib_path.c_str()));

#ifdef _DEBUG
  debug_dump_sessionState(*this);
#endif
}

// Does a copy of the information part of sessionState only.
sessionState & sessionState::clone_info_part(const sessionState &ss) noexcept {
  supervisor_pid = ss.supervisor_pid;
  child_process_max_count = ss.child_process_max_count;
  spartanMainEntryPoint = ss.spartanMainEntryPoint;
  spartanGetStatusEntryPoint = ss.spartanGetStatusEntryPoint;
  spartanSupervisorShutdownEntryPoint = ss.spartanSupervisorShutdownEntryPoint;
  spartanChildNotifyEntryPoint = ss.spartanChildNotifyEntryPoint;
  spartanChildCompletionNotifyEntryPoint = ss.spartanChildCompletionNotifyEntryPoint;
  spartanSupervisorEntryPoint = ss.spartanSupervisorEntryPoint;
  spartanChildProcessorEntryPoint = ss.spartanChildProcessorEntryPoint;
  spartanChildProcessorCommands = ss.spartanChildProcessorCommands;
  systemClassPath = ss.systemClassPath;
  spSpartanSupervisorCommands = ss.spSpartanSupervisorCommands;
  spSpartanChildProcessorCommands = ss.spSpartanChildProcessorCommands;
  spSerializedSystemProperties = ss.spSerializedSystemProperties;
  spartanLoggingLevel = ss.spartanLoggingLevel;
  jvmlib_path = ss.jvmlib_path;
  return *this;
}

// Is not a true move assignment operator as it actually
// copies the information part of sessionState and then
// moves the smart pointer state of the jvm shared library
// handle, the instantiated jvm object, and jvm environment
sessionState & sessionState::operator=(sessionState && ss) noexcept {
  // copy
  clone_info_part(ss);
  // move
  libjvm_sp = std::move(ss.libjvm_sp);
  jvm_sp = std::move(ss.jvm_sp);
  env_sp = std::move(ss.env_sp);
  return *this;
}

void sessionState::create_jvm(const char *jvm_override_optns) {
  const jvm_create_t jvm_rt = ::create_jvm(libjvm_sp.get(), jvm_override_optns);
  jvm_sp.reset(std::get<0>(jvm_rt));
  env_sp.reset(std::get<1>(jvm_rt));
}

template <typename T>
static std::ostream& stream_vec_out(std::ostream &os, const std::shared_ptr<std::vector<T>> &self,
                                    const char delim = '\n')
{
  os << typeid(std::vector<T>).name() << '\n';
  if (self) {
    const auto count = self->size();
    os << count << '\n';
    for(const auto &elem : *self) {
      os << elem << delim;
    }
  } else {
    using vec_size_t = decltype(self->size());
    os << static_cast<vec_size_t>(0) << '\n';
  }
  return os << '\n';
}

template <typename T>
static std::istream& stream_in_vec_elm(std::istream &is, T &t) {
  is >> t;
  char delim;
  is.getline(&delim, 1);
  return is;
}

template <typename T>
using stream_in_vec_elm_t = decltype(stream_in_vec_elm<T>);

template <typename T>
static std::istream& stream_vec_in(std::istream &is, std::shared_ptr<std::vector<T>> &self,
                                   stream_in_vec_elm_t<T> stream_in_elm = stream_in_vec_elm)
{
  std::string vec_type;
  std::getline(is, vec_type, '\n');
  using vec_size_t = decltype(self->size());
  vec_size_t count = 0;
  is >> count;
  char newline;
  is.getline(&newline, 1);
  if (count > 0) {
    self.reset(new std::vector<T>());
    self->reserve(count);
    T t;
    for(unsigned int i = 0; i < count; i++) {
      stream_in_elm(is, t);
      self->push_back(std::move(t));
    }
  }
  is.getline(&newline, 1);
  return is;
}

std::ostream& operator << (std::ostream &os, const sessionState &self) {
  using self_t = std::remove_reference<decltype(self)>::type;
  os << typeid(self_t).name() << '\n';
  os << self.supervisor_pid << '\n';
  os << self.child_process_max_count << '\n';
  os << self.spartanMainEntryPoint << '\n';
  os << self.spartanGetStatusEntryPoint << '\n';
  os << self.spartanSupervisorShutdownEntryPoint << '\n';
  os << self.spartanChildNotifyEntryPoint << '\n';
  os << self.spartanChildCompletionNotifyEntryPoint << '\n';
  os << self.spartanSupervisorEntryPoint << '\n';
  os << self.spartanChildProcessorEntryPoint << '\n';
  os << self.spartanChildProcessorCommands << '\n';
  os << self.systemClassPath << '\n';
  stream_vec_out(os, self.spSpartanSupervisorCommands) << '\n';
  stream_vec_out(os, self.spSpartanChildProcessorCommands) << '\n';
  stream_vec_out(os, self.spSerializedSystemProperties, '\r') << '\n';
  os << self.spartanLoggingLevel << '\n';
  os << self.jvmlib_path << '\n';
  return os;
}

std::istream& operator >> (std::istream &is, sessionState &self) {
  std::string type_name;
  std::getline(is, type_name, '\n');
  is >> self.supervisor_pid;
  char newline;
  is.getline(&newline, 1);
  is >> self.child_process_max_count;
  is.getline(&newline, 1);
  is >> self.spartanMainEntryPoint;
  is.getline(&newline, 1);
  is >> self.spartanGetStatusEntryPoint;
  is.getline(&newline, 1);
  is >> self.spartanSupervisorShutdownEntryPoint;
  is.getline(&newline, 1);
  is >> self.spartanChildNotifyEntryPoint;
  is.getline(&newline, 1);
  is >> self.spartanChildCompletionNotifyEntryPoint;
  is.getline(&newline, 1);
  is >> self.spartanSupervisorEntryPoint;
  is.getline(&newline, 1);
  is >> self.spartanChildProcessorEntryPoint;
  is.getline(&newline, 1);
  std::getline(is, self.spartanChildProcessorCommands, '\n');
  std::getline(is, self.systemClassPath, '\n');
  stream_vec_in(is, self.spSpartanSupervisorCommands);
  is.getline(&newline, 1);
  stream_vec_in(is, self.spSpartanChildProcessorCommands);
  is.getline(&newline, 1);
  stream_vec_in(is, self.spSerializedSystemProperties, [](std::istream &in_strm, std::string &str) -> std::istream& {
    str.clear();
    std::getline(in_strm, str, '\r');
    return in_strm;
  });
  is.getline(&newline, 1);
  std::getline(is, self.spartanLoggingLevel, '\n');
  std::getline(is, self.jvmlib_path, '\n');
  return is;
}

/**
 * Locates the Spartan .jar file and prepends the JVM bootclasspath option specifying the
 * Spartan .jar path. The JVM command line string (the argument passed in) is then appended.
 *
 * The function then looks to find presence of JVM option -Djava.library.path.
 *
 * If the option is found then it prepends the executable's path to the list of paths defined
 * by it.
 *
 * If the option is not found, then it appends the option, specifying the executable's path.
 *
 * NOTE: The Spartan native code shared library will be locatable via the executable's path.
 *
 * @param jvm_cmd_line_args the Java JVM command line arguments (as read from config.ini)
 * @return the Java JVM command line arguments now incorporating directory paths to Spartan
 * install directory for locating Spartan.jar and libspartan.so
 */
static string_view prepend_to_java_library_path(const string_view jvm_cmd_line_args) {
  static const char* const func_name = __FUNCTION__;
  const string_view executable_path_strv{ executable_dir() };
  std::string spartan_jar_path_str{};
  std::string rtn_str{};
  rtn_str.resize(jvm_cmd_line_args.size() + 2048);

  // NOTE: it's a fatal condition if unable to locate Spartan .jar file, so will log error and terminate if not found
  // (only looks in the directory tree where the spartan executable resides, as that is where should be installed at).
  // The first file encountered that starts with "Spartan" and ends with ".jar" will be the one selected; search match
  // is case insensitive.
  try {
    if (findfiles(executable_path_strv.c_str(),
                  [&spartan_jar_path_str](const char *const filepath, const char *const filename) {
                    const char *file_extent = nullptr;
                    if (strncasecmp(filename, spartan_strv.c_str(), spartan_strv.size()) == 0 &&
                        (file_extent = strcasestr(filename, jar_file_extent_strv.c_str())) != nullptr)
                    {
                      const string_view file_name_sv{filename};
                      const size_t file_name_base_size = file_extent - filename;
                      if ((jar_file_extent_strv.size() + file_name_base_size) == file_name_sv.size()) {
                        spartan_jar_path_str = filepath;
                        return true;
                      }
                    }
                    return false;
                  }))
    {
      rtn_str = boot_classpath_optn_strv.c_str();
      rtn_str += spartan_jar_path_str;
    } else {
      log(LL::FATAL, "failed to find the %s %s file", spartan_strv.c_str(), jar_file_extent_strv.c_str());
      _exit(1);
    }
  } catch (findfiles_exception &ex) {
    log(LL::FATAL, "failed to find the %s %s file:\n\t%s: %s",
        spartan_strv.c_str(), jar_file_extent_strv.c_str(), ex.name(), ex.what());
    _exit(1);
  }

  auto const log_returned_jvm_cmd_line_args = [&rtn_str]() {
    if (is_trace_level()) {
      log(LL::TRACE, "%s() returned JVM command line arguments:\n\t%s", func_name, rtn_str.c_str());
    }
  };

  if (!jvm_cmd_line_args.empty()) {
    int argc = 0;
    const char**argv = nullptr;

    const auto rtn = poptParseArgvString(jvm_cmd_line_args.c_str(), &argc, &argv);

    if (is_trace_level()) {
      log(LL::TRACE, "%s() parse of jvm_cmd_line_args: rtn: %d, argc: %d", func_name, rtn, argc);
    }
    if (rtn != 0) {
      log(LL::FATAL, "JVM options could not be parsed: %s\n\t%s\n", poptStrerror(rtn), jvm_cmd_line_args.c_str());
      _exit(1);
    }

    // provide a RAII cleanup for the popt argv allocation
    std::unique_ptr<const char*, decltype(&std::free)> raii_argv_sp{ argv, &std::free };
    std::unique_ptr<char, decltype(&std::free)> raii_rplc_optn_sp{ nullptr, &std::free };

    auto const log_found_matching_arg = [](int argc_index, const char *argv_item) {
      if (is_trace_level()) {
        printf("\tfound a matching argv[%d]: %s\n", argc_index, argv_item);
      }
    };

    // search for options: '-Djava.library.path=' and '-Xbootclasspath/a:'
    for (int i = 0; i < argc; i++) {
      const string_view arg{ argv[i] };
      if (is_trace_level()) {
        printf("\targv[%d]: %s\n", i, (char*)arg.c_str());
      }
      if (strncasecmp(arg.c_str(), java_lib_path_optn_strv.c_str(), java_lib_path_optn_strv.size()) == 0) {
        auto optn = strcasestr(arg.c_str(), java_lib_path_optn_strv.c_str());
        assert(optn != nullptr);
        if (optn == nullptr) continue;
        log_found_matching_arg(i, optn);
        const string_view optn_value{optn + java_lib_path_optn_strv.size()};
        std::string replacement_java_lib_path_optn{};
        replacement_java_lib_path_optn.resize(executable_path_strv.size() + (2 * sizeof(PATH_SEPARATOR)) + arg.size());
        replacement_java_lib_path_optn += java_lib_path_optn_strv.c_str();
        replacement_java_lib_path_optn += executable_path_strv.c_str();
        replacement_java_lib_path_optn += PATH_SEPARATOR;
        replacement_java_lib_path_optn += optn_value.c_str();
        raii_rplc_optn_sp.reset(strndup(replacement_java_lib_path_optn.c_str(), replacement_java_lib_path_optn.size()));
        argv[i] = raii_rplc_optn_sp.get();
      } else if (strncasecmp(arg.c_str(), boot_classpath_optn_strv.c_str(), boot_classpath_optn_strv.size()) == 0) {
        auto optn = strcasestr(arg.c_str(), boot_classpath_optn_strv.c_str());
        assert(optn != nullptr);
        if (optn == nullptr) continue;
        log_found_matching_arg(i, optn);
        const string_view optn_value{optn + boot_classpath_optn_strv.size()};
        rtn_str += PATH_SEPARATOR;
        rtn_str += optn_value.c_str();
        argv[i] = "";
      }
    }

    if (raii_rplc_optn_sp) {
      // now run through the argv array, append each arg[i] into string buffer and put
      // double quotes around it; a space character delimiter separates each argument
      for (int i = 0; i < argc; i++) {
        const string_view arg{ argv[i] };
        if (arg.empty()) continue;
        rtn_str += ' ';
        rtn_str += '"';
        rtn_str += arg.c_str();
        rtn_str += '"';
      }
    } else {
      // append the input JVM command line string as is into the string buffer
      rtn_str += ' ';
      rtn_str += jvm_cmd_line_args.c_str();
    }

    log_returned_jvm_cmd_line_args();
    return string_view{strndup(rtn_str.c_str(), rtn_str.size()), rtn_str.size()}; // return the updated JVM command line
  }

  // command line option -Djava.library.path not found so synthesize one with the executable's path as its value
  rtn_str += ' ';
  rtn_str += java_lib_path_optn_strv.c_str();
  rtn_str += executable_path_strv.c_str();

  log_returned_jvm_cmd_line_args();
  return string_view{strndup(rtn_str.c_str(), rtn_str.size()), rtn_str.size()}; // return the updated JVM command line
}