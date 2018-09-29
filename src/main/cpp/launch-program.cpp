/* launch-program.cpp

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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <cassert>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/syscall.h>
#include <spawn.h>
#include <cxxabi.h>
#include <algorithm>
#include "session-state.h"
#include "process-cmd-dispatch-info.h"
#include "path-concat.h"
#include "format2str.h"
#include "fifo-pipe.h"
#include "log.h"
#include "spartan_LaunchProgram.h"
#include "launch-program.h"

using logger::log;
using logger::LL;

DECL_EXCEPTION(find_program_path)
DECL_EXCEPTION(create_uds_socket)
DECL_EXCEPTION(bind_uds_socket_name)
DECL_EXCEPTION(obtain_rsp_stream)
DECL_EXCEPTION(spawn_program)
DECL_EXCEPTION(interrupted)

extern char **environ;

static volatile bool termination_flag = false;

// forward declaration
static std::string find_program_path(const char * const prog, const char * const path_var_name);

namespace launch_program {
  static std::string s_progpath;

  // NOTE: this property must be set prior to using Java_spartan_LaunchProgram_invokeCommand()
  SO_EXPORT void set_progpath(const char *const progpath) {
    s_progpath = std::string(progpath);
  }

  SO_EXPORT std::tuple<std::string,bool> try_resolve_program_path(const char * const prog,
                                                                  const char * const path_var_name)
  {
    try {
      return std::make_tuple(find_program_path(prog, path_var_name), true);
    } catch(const find_program_path_exception&) {
      return std::make_tuple(std::string(prog), false);
    }
  }

  SO_EXPORT void fd_cleanup_no_delete(fd_wrapper_t *p) {
    if (p != nullptr && p->fd != -1) {
      close(p->fd);
      p->fd = -1;
    }
  }

  SO_EXPORT void fd_cleanup_with_delete(fd_wrapper_t *p) {
    if (p != nullptr && p->fd != -1) {
      close(p->fd);
      p->fd = -1;
    }
    delete p;
  }

  SO_EXPORT void init_sockaddr(std::string const &uds_sock_name, sockaddr_un &addr, socklen_t &addr_len) {
    memset(&addr, 0, sizeof(sockaddr_un));
    addr.sun_family = AF_UNIX;
    auto const path_buf_end = sizeof(addr.sun_path) - 1;
    strncpy(addr.sun_path, uds_sock_name.c_str(), path_buf_end);
    addr.sun_path[path_buf_end] = '\0';
    addr.sun_path[0] = '\0';
    addr_len = sizeof(sockaddr_un) - (sizeof(addr.sun_path) - uds_sock_name.size());
  }

  SO_EXPORT fd_wrapper_sp_t create_uds_socket(std::function<std::string(int)> get_errmsg) {
    auto fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
      throw create_uds_socket_exception{ get_errmsg(errno) };
    }

    return fd_wrapper_sp_t{ new fd_wrapper_t{ fd }, &fd_cleanup_with_delete };
  }

  SO_EXPORT std::tuple<fd_wrapper_sp_t, std::string> bind_uds_socket_name(const char* const sub_cmd) {
    auto const progname = [](const char * const path) -> std::string {
      char * const dup_path = strdupa(path);
      return std::string(basename(dup_path));
    }(s_progpath.c_str());

    auto const uds_socket_name = make_fifo_pipe_name(progname.c_str(), "JLauncher_UDS");

    auto socket_fd_sp = create_uds_socket([sub_cmd](int err_no) -> std::string {
      const char err_msg_fmt[] = "failed creating parent uds socket for i/o to spawned program subcommand %s: %s";
      return format2str(err_msg_fmt, sub_cmd, strerror(err_no));
    });

    sockaddr_un server_address;
    socklen_t address_length;
    init_sockaddr(uds_socket_name, server_address, address_length);

    auto rc = bind(socket_fd_sp->fd, (const sockaddr*) &server_address, address_length);
    if (rc < 0) {
      const char err_msg_fmt[] = "failed binding parent uds socket for i/o to spawned program subcommand %s: %s";
      auto err_msg = format2str(err_msg_fmt, sub_cmd, strerror(errno));
      throw bind_uds_socket_name_exception{ std::move(err_msg) };
    }

    return std::make_tuple(std::move(socket_fd_sp), std::move(uds_socket_name));
  }

  SO_EXPORT std::tuple<pid_t, fd_wrapper_sp_t> obtain_response_stream(std::string const &uds_socket_name,
                                                                      fd_wrapper_sp_t socket_read_fd_sp)
  {
    static const char* const func_name = __FUNCTION__;

    sockaddr_un server_address;
    socklen_t address_length;
    init_sockaddr(uds_socket_name, server_address, address_length);

    pid_buffer_t pid_buffer;
    memset(&pid_buffer, 0, sizeof(pid_buffer));

    int line_nbr = __LINE__ + 1;
    auto bytes_received = recvfrom( socket_read_fd_sp->fd,
                                    &pid_buffer,
                                    sizeof(pid_buffer),
                                    0,
                                    (sockaddr *) &server_address,
                                    &address_length);
    if (bytes_received < 0) {
      const char err_msg_fmt[] = "%d: %s() -> recvfrom(): failed reading pid and fd count from uds %s socket:\n\t%s";
      auto err_msg = format2str(err_msg_fmt, line_nbr, func_name, uds_socket_name.c_str(), strerror(errno));
      throw obtain_rsp_stream_exception{ std::move(err_msg) };
    }
    assert(bytes_received == (long) sizeof(pid_buffer));
    assert(pid_buffer.pid > 0 && pid_buffer.fd_rtn_count > 0);

    if (pid_buffer.fd_rtn_count != 1) {
      const char err_msg_fmt[] = "%d: %s() -> expected exactly 1 read pipe fd count via uds %s socket - not %d";
      auto err_msg = format2str(err_msg_fmt, line_nbr, func_name, uds_socket_name.c_str(), pid_buffer.fd_rtn_count);
      throw obtain_rsp_stream_exception{ std::move(err_msg) };
    }

    init_sockaddr(uds_socket_name, server_address, address_length);

    msghdr client_recv_msg;
    memset(&client_recv_msg, 0, sizeof(client_recv_msg));
    client_recv_msg.msg_name = &server_address;
    client_recv_msg.msg_namelen = address_length;
    pipe_fds_buffer_t cmsg_payload;
    memset(&cmsg_payload, 0, sizeof(cmsg_payload));
    client_recv_msg.msg_control = &cmsg_payload;
    client_recv_msg.msg_controllen = sizeof(cmsg_payload); // necessary for CMSG_FIRSTHDR to return the correct value

    auto rc = recvmsg(socket_read_fd_sp->fd, &client_recv_msg, 0); line_nbr = __LINE__;
    if (rc < 0) {
      const char err_msg_fmt[] = "%d: %s() -> recvmsg(): no read pipe fd returned from uds %s socket:\n\t%s";
      auto err_msg = format2str(err_msg_fmt, line_nbr, func_name, uds_socket_name.c_str(), strerror(errno));
      throw obtain_rsp_stream_exception{ std::move(err_msg) };
    }
    const cmsghdr* const cmsg = CMSG_FIRSTHDR(&client_recv_msg);
    assert(cmsg != nullptr);
    assert(cmsg->cmsg_type == SCM_RIGHTS);
    line_nbr = __LINE__ + 1;
    if (cmsg == nullptr || cmsg->cmsg_type != SCM_RIGHTS) {
      const char *const err_msg_fmt = "%d: %s() -> recvmsg(): no read pipe fd returned from uds %s socket:\n\t%s";
      auto err_msg = format2str(err_msg_fmt, line_nbr, func_name, uds_socket_name.c_str(), "invalid datagram message");
      throw obtain_rsp_stream_exception{ std::move(err_msg) };
    }

    assert(cmsg_payload.pipe_fds[0] > 0);
    fd_wrapper_sp_t read_stream_fd_sp{ new fd_wrapper_t{ cmsg_payload.pipe_fds[0] }, &fd_cleanup_with_delete };

    return std::make_tuple(pid_buffer.pid, std::move(read_stream_fd_sp));
  }
}

using namespace launch_program;

inline const char *progpath() { return launch_program::s_progpath.c_str(); }

static std::string get_env_var(const char * const name) {
  char * const val = getenv(name);
  auto rtn_str( val != nullptr ? std::string(val) : std::string() );
  return rtn_str;
}

static std::string find_program_path(const char * const prog, const char * const path_var_name) {
  const std::string path_env_var( get_env_var(path_var_name) );

  if (path_env_var.size() <= 0) {
    const char * const err_msg_fmt = "there is no %s environment variable defined";
    throw find_program_path_exception(format2str(err_msg_fmt, path_var_name));
  }

  const char * const path_env_var_dup = strdupa(path_env_var.c_str());

  static const char * const delim = ":";
  char *save = nullptr;
  const char * path = strtok_r(const_cast<char*>(path_env_var_dup), delim, &save);

  while(path != nullptr) {
    log(LL::DEBUG, "'%s'", path);
    const auto len = strlen(path);
    const char end_char = path[len - 1];
    const char * const fmt = end_char == '/' ? "%s%s" : "%s/%s";
    auto full_path( format2str(fmt, path, prog) );
    log(LL::DEBUG, "'%s'", full_path.c_str());
    // check to see if program file path exist
    struct stat statbuf;
    if (stat(full_path.c_str(), &statbuf) != -1 && ((statbuf.st_mode & S_IFMT) == S_IFREG ||
                                                    (statbuf.st_mode & S_IFMT) == S_IFLNK)) {
      return full_path;
    }
    path = strtok_r(nullptr, delim, &save);
  }

  const char * const err_msg_fmt = "could not locate program '%s' via %s environment variable";
  throw find_program_path_exception(format2str(err_msg_fmt, prog, path_var_name));
}

static std::tuple<pid_t, fd_wrapper_sp_t, std::string> spawn_program(int argc, char **argv,
                                                                     const char * const prog_file,
                                                                     const char * const prog_path)
{
  auto const argc_dup = argc + 1; // bump up by one for added -pipe= option
  char**const argv_dup = (char**) alloca((argc_dup + 1) * sizeof(argv[0])); // reserve nullptr entry at array end too
  argv_dup[0] = strdupa(prog_path); // file path of program to be spawned
  argv_dup[1] = nullptr; // command line option conveys pipe file descriptors to spawned program
  for(int i = 2, j = 1; j < argc; i++, j++) {
    argv_dup[i] = argv[j];
  }
  argv_dup[argc_dup] = nullptr; // sentinel entry at end of argv array (and argv array convention)

  auto uds_socket_name = make_fifo_pipe_name(progpath(), "JLauncher_UDS");
  auto const pipe_optn = format2str("-pipe=%s", uds_socket_name.c_str());
  argv_dup[1] = strdupa(pipe_optn.c_str()); // set -pipe=... as command line arg to spawned program

  fd_wrapper_sp_t read_fd_sp = create_uds_socket([subcmd = argv_dup[2], &uds_socket_name](int err_no) -> std::string {
    const char err_msg_fmt[] = "failed creating parent unix uds %s socket for i/o to spawned program subcommand %s: %s";
    return format2str(err_msg_fmt, uds_socket_name.c_str(), subcmd, strerror(err_no));
  });

  sockaddr_un server_address;
  socklen_t address_length;
  init_sockaddr(uds_socket_name, server_address, address_length);

  if (bind(read_fd_sp->fd, (const sockaddr*) &server_address, address_length) < 0) {
    const char err_msg_fmt[] = "failed binding parent unix uds %s socket for i/o to spawned program subcommand %s: %s";
    throw bind_uds_socket_name_exception(format2str(err_msg_fmt, uds_socket_name.c_str(), argv_dup[2], strerror(errno)));
  }

  posix_spawnattr_t attr;
  auto rtn = posix_spawnattr_init(&attr);
  if (rtn != 0) {
    const char err_msg_fmt[] = "could not initialize spawn attributes object: %d";
    throw spawn_program_exception(format2str(err_msg_fmt, rtn));
  }
  auto const cleanup = [](posix_spawnattr_t *p) {
    if (p != nullptr) {
      posix_spawnattr_destroy(p);
    }
  };
  std::unique_ptr<posix_spawnattr_t, decltype(cleanup)> attr_sp(&attr, cleanup);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_USEVFORK);

  log(LL::DEBUG, "number of spawned program %s command line args: %d", prog_file, argc_dup);
  for (int i = 0; i < argc_dup; i++) {
    log(LL::DEBUG, "'%s'", argv_dup[i]);
  }

  pid_t pid = 0;
  rtn = posix_spawnp(&pid, prog_file, nullptr, &attr, argv_dup, environ);
  if (rtn != 0) {
    const char err_msg_fmt[] = "invocation of posix_spawnp() failed: %d";
    throw spawn_program_exception(format2str(err_msg_fmt, rtn));
  }

  return std::make_tuple(pid, std::move(read_fd_sp), std::move(uds_socket_name));
}

static std::tuple<pid_t, fd_wrapper_sp_t> launch_program_helper(int argc, char **argv, std::string& prog_path) {
  const char *const prog_name = strdupa(prog_path.c_str()); // starts out as just program name, so copy this to retain
  if (strchr(prog_name, '/') == nullptr && strchr(prog_name, '\\') == nullptr) {
    prog_path = find_program_path(prog_name, "PATH"); // determine fully qualified path to the program
  } else {
    // verify that the specified program path exist and is a file or symbolic link
    struct stat statbuf;
    if (stat(prog_name, &statbuf) == -1 || !((statbuf.st_mode & S_IFMT) == S_IFREG ||
                                             (statbuf.st_mode & S_IFMT) == S_IFLNK))
    {
      const char err_msg_fmt[] = "specified program path '%s' invalid: %s";
      throw find_program_path_exception(format2str(err_msg_fmt, prog_name, strerror(errno)));
    }
  }

  auto rslt = spawn_program(argc, argv, prog_name, prog_path.c_str()); // spawn the temporary launcher process
  pid_t const pid = std::get<0>(rslt);
  fd_wrapper_sp_t socket_read_fd_sp{ std::move(std::get<1>(rslt)) };
  std::string uds_socket_name{ std::move(std::get<2>(rslt)) };

  auto rslt2 = obtain_response_stream(uds_socket_name, std::move(socket_read_fd_sp));
  pid_t const child_pid = std::get<0>(rslt2);
  fd_wrapper_sp_t child_read_fd_sp{ std::move(std::get<1>(rslt2)) };

  auto const flags = fcntl(child_read_fd_sp->fd, F_GETFL, 0);
  fcntl(child_read_fd_sp->fd, F_SETFL, flags & ~O_NONBLOCK);

  int status = 0;
  do {
    if (waitpid(pid, &status, 0) == -1) {
      const char err_msg_fmt[] = "failed waiting for launcher process (pid:%d): %s";
      throw spawn_program_exception(format2str(err_msg_fmt, pid, strerror(errno)));
    }
    if (WIFSIGNALED(status) || WIFSTOPPED(status)) {
      const char err_msg_fmt[] = "interrupted waiting for launcher process (pid:%d)";
      throw interrupted_exception(format2str(err_msg_fmt, pid));
    }
  } while (!WIFEXITED(status) && !WIFSIGNALED(status));

  log(LL::DEBUG, "%s(): ***** spawned launcher process (pid:%d) of child program subcommand %s completed *****\n",
      __FUNCTION__, pid, argv[1]);

  log(LL::DEBUG, "%s(): ***** spawned child program subcommand %s pid: %d *****\n", __FUNCTION__, argv[1], child_pid);

  // return pid, fd, and uds socket name per launched child process
  return std::make_tuple(child_pid, std::move(child_read_fd_sp));
}

static const char * const invoke_child_cmd_errmsg_fmt = "unknown child command: %s";
static const char * const spawn_failed_errmsg1_fmt  = "spawn of '%s' failed:\n\t%s: %s";
static const char * const spawn_failed_errmsg2_fmt  = "spawn of '%s' failed:\n\tprocess %d terminating due to unhandled"
                                                      " exception of type %s";
static const char * const spawn_failed_errmsg3_fmt  = "spawn of '%s' failed; could not load class '%s'";
static const char * const spawn_failed_errmsg4_fmt  = "spawn of '%s' failed; could find method '%s'";
static const char * const spawn_failed_errmsg5_fmt  = "spawn of '%s' failed; failed retrieving fieldId %s";
static const char * const spawn_failed_errmsg6_fmt  = "spawn of '%s' failed; failed allocating JNI Java object '%s'";
static const char * const killpid_failed_errmsg_fmt = "kill(pid:%d,%s) did not succeed: %s";
static const char * const getpgid_failed_errmsg_fmt = "getpgid(pid:%d) did not succeed: %s";
static const char * const open_pidfile_failed_errmsg_fmt = "failed to open process pid file: \"%s\"\n\t%s";
static const char * const flock_pidfile_failed_errmsg_fmt = "failed exclusive locking of process pid file: \"%s\"\n\t%s";
static const char * const ctor_name                 = "<init>";
static const char * const invkcmd_excptn_cls        = "spartan/Spartan$InvokeCommandException";
static const char * const killpid_excptn_cls        = "spartan/Spartan$KillProcessException";
static const char * const killpg_excptn_cls         = "spartan/Spartan$KillProcessGroupException";


static void throw_java_exception(JNIEnv *env, const char *excptn_cls, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const std::string rslt(vformat2str(fmt, ap));
  va_end(ap);
  jclass const ex_cls = env->FindClass(excptn_cls);
  assert(ex_cls != nullptr);
  if (ex_cls != nullptr) {
    env->ThrowNew(ex_cls, rslt.c_str());
    env->DeleteLocalRef(ex_cls);
  }
}

template<typename T>
static bool check_result(JNIEnv *env, const char *exception_cls, T item,
                  const char *fmt, const char *prog_path, const char *desc) {
  if (item == nullptr) {
    jclass ex_cls = env->FindClass(exception_cls);
    assert(ex_cls != nullptr);
    if (ex_cls != nullptr) {
      env->ThrowNew(ex_cls, format2str(fmt, prog_path, desc).c_str());
      env->DeleteLocalRef(ex_cls);
    }
    return false;
  }
  return true;
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    log
 * Signature: (ILjava/lang/String;)V
 */
extern "C" JNIEXPORT void JNICALL Java_spartan_LaunchProgram_log
    (JNIEnv *env, jclass /*cls*/, jint level, jstring msg) {
  struct {
    jboolean    isCopy;
    jstring     j_str;
    const char* c_str;
  } jstr{static_cast<jboolean>(false), msg, nullptr};
  jstr.c_str = env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
  using jstr_t = decltype(jstr);
  auto const cleanup = [env](jstr_t *p) {
    assert(p != nullptr);
    if (p->isCopy == JNI_TRUE) {
      env->ReleaseStringUTFChars(p->j_str, p->c_str);
    }
    env->DeleteLocalRef(p->j_str);
  };
  std::unique_ptr<jstr_t, decltype(cleanup)> sp_msg(&jstr, cleanup);
  log((LL) level, "%s", sp_msg->c_str);
}

/*
 * Function:  invoke_spartan_subcommand
 * Signature: (Ljava/lang/String;[Ljava/lang/String;)Lspartan/Spartan/InvokeResponse;
 */
static jobject JNICALL invoke_spartan_subcommand(JNIEnv *env, jclass /*cls*/, jstring progName, jobjectArray args) {
  const jint argc = env->GetArrayLength(args);
  struct argv_str_t {
    jboolean  isCopy;
    jstring   j_str;
  };
  auto * const argv_strs = (argv_str_t*)  alloca((argc + 1) * sizeof(argv_str_t));
  const auto * * const c_strs = (const char**) alloca((argc + 2) * sizeof(const char*));
  c_strs[argc + 1] = nullptr; // argv convention of end-of-array null ptr sentinel
  for(int i = 0; i <= argc; i++) {
    auto &argv_str = argv_strs[i];
    argv_str.isCopy = static_cast<jboolean >(false);
    argv_str.j_str  = nullptr;
    c_strs[i]       = nullptr;
  }

  auto const cleanup = [env,argc,c_strs](argv_str_t *p) {
    if (p != nullptr) {
      for(int i = 0; i <= argc; i++) {
        auto &argv_str_elm = p[i];
        if (argv_str_elm.isCopy == JNI_TRUE) {
          env->ReleaseStringUTFChars(argv_str_elm.j_str, c_strs[i]);
        }
        env->DeleteLocalRef(argv_str_elm.j_str);
      }
    }
  };
  std::unique_ptr<argv_str_t, decltype(cleanup)> argv_strs_sp(argv_strs, cleanup);

  // the zero index entry is for the program name
  argv_strs[0].j_str = progName;
  c_strs[0] = env->GetStringUTFChars(progName, &argv_strs[0].isCopy);

  // the remaining entries are for the argv strings
  for(int i = 1; i <= argc; i++) {
    auto &argv_str = argv_strs[i];
    argv_str.j_str = (jstring) env->GetObjectArrayElement(args, i - 1);
    c_strs[i]      = env->GetStringUTFChars(argv_str.j_str, &argv_str.isCopy);
  }

  std::string prog_path(c_strs[0]);
  pid_t child_pid = 0;
  fd_wrapper_sp_t child_read_fd_sp{ nullptr, &fd_cleanup_with_delete };
  try {
    auto rslt = launch_program_helper(argc + 1, (char **) c_strs, prog_path);
    child_pid = std::get<0>(rslt);
    child_read_fd_sp = std::move(std::get<1>(rslt));
  } catch(const interrupted_exception& ex) {
    jclass const ex_cls = env->FindClass("java/lang/InterruptedException");
    assert(ex_cls != nullptr);
    env->ThrowNew(ex_cls, ex.what());
    return nullptr;
  } catch(const spartan_exception& ex) {
    throw_java_exception(env, invkcmd_excptn_cls, spawn_failed_errmsg1_fmt, prog_path.c_str(), ex.name(), ex.what());
    return nullptr;
  } catch(const std::exception& ex) {
    throw_java_exception(env, invkcmd_excptn_cls, spawn_failed_errmsg1_fmt, prog_path.c_str(), typeid(ex).name(), ex.what());
    return nullptr;
  } catch(...) {
    const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
    throw_java_exception(env, invkcmd_excptn_cls, spawn_failed_errmsg2_fmt, prog_path.c_str(), getpid(), ex_nm.c_str());
    return nullptr;
  }

  auto const find_class = [env,&prog_path](const char *cls_name) -> jclass {
    jclass const found_cls = env->FindClass(cls_name);
    const char * const exception_cls = "java/lang/ClassNotFoundException";
    const char * const err_msg_fmt = spawn_failed_errmsg3_fmt;
    return check_result(env, exception_cls, found_cls, err_msg_fmt, prog_path.c_str(), cls_name) ? found_cls : nullptr;
  };

  auto const get_method = [env,&prog_path](jclass m_cls, const char *method_name, const char *method_sig) -> jmethodID {
    auto const methID = env->GetMethodID(m_cls, method_name, method_sig);
    const char * const err_msg_fmt = spawn_failed_errmsg4_fmt;
    return check_result(env, invkcmd_excptn_cls, methID, err_msg_fmt, prog_path.c_str(), method_name) ? methID : nullptr;
  };

  auto const get_fieldid = [env,&prog_path](jclass f_cls, const char *field_name, const char *field_sig,
                                            const char *desc) -> jfieldID {
    auto const field_fd = env->GetFieldID(f_cls, field_name, field_sig);
    const char * const err_msg_fmt = spawn_failed_errmsg5_fmt;
    return check_result(env, invkcmd_excptn_cls, field_fd, err_msg_fmt, prog_path.c_str(), desc) ? field_fd : nullptr;
  };

  auto const check_new_obj = [env,&prog_path](jobject obj, const char *desc) -> bool {
    const char * const err_msg_fmt = spawn_failed_errmsg6_fmt;
    return check_result(env, invkcmd_excptn_cls, obj, err_msg_fmt, prog_path.c_str(), desc);
  };

  // FileDescriptor class and ctor
  auto const fdesc_cls = find_class("java/io/FileDescriptor");
  if (fdesc_cls == nullptr) return nullptr;

  auto const fdesc_ctor = get_method(fdesc_cls, ctor_name, "()V");
  if (fdesc_ctor == nullptr) return nullptr;

  // java.io.FileInputStream class and ctor
  auto const file_input_strm_cls = find_class("java/io/FileInputStream");
  if (file_input_strm_cls == nullptr) return nullptr;

  auto const file_input_strm_ctor = get_method(file_input_strm_cls, ctor_name, "(Ljava/io/FileDescriptor;)V");
  if (file_input_strm_ctor == nullptr) return nullptr;

  // Spartan.InvokeResponse class and ctor
  auto const invoke_rsp_cls = find_class("spartan/Spartan$InvokeResponse");
  if (invoke_rsp_cls == nullptr) return nullptr;

  auto const invoke_rsp_ctor = get_method(invoke_rsp_cls, ctor_name, "(ILjava/io/InputStream;)V");
  if (invoke_rsp_ctor == nullptr) return nullptr;

  // construct a new FileDescriptor
  auto const fdesc = env->NewObject(fdesc_cls, fdesc_ctor);
  if (!check_new_obj(fdesc, "FileDescriptor for input pipe fd")) return nullptr;

  struct {
    jobject const fdesc;
  }
      args_wrpr{ fdesc };
  using args_wrpr_t = decltype(args_wrpr);
  auto const deref_jobjs = [env](args_wrpr_t *p) {
    if (p != nullptr) {
      env->DeleteLocalRef(p->fdesc);
    }
  };
  std::unique_ptr<args_wrpr_t, decltype(deref_jobjs)> sp_deref_jobjs(&args_wrpr, deref_jobjs);

  // poke the "fd" field with the file descriptor
  auto const field_id = get_fieldid(fdesc_cls, "fd", "I", "on file descriptor object");
  if (field_id == nullptr) return nullptr;
  env->SetIntField(fdesc, field_id, child_read_fd_sp->fd);

  // construct a new java.io.FileInputStream
  auto const file_input_strm_obj = env->NewObject(file_input_strm_cls, file_input_strm_ctor, fdesc);
  if (!check_new_obj(file_input_strm_obj, "FileInputStream per the input pipe fd")) return nullptr;

  // construct a new Spartan.InvokeResponse and populate object with return results
  auto const invoke_rsp_obj = env->NewObject(invoke_rsp_cls, invoke_rsp_ctor, child_pid, file_input_strm_obj);
  if (!check_new_obj(invoke_rsp_obj, "Spartan.InvokeResponse as result of spawned program operation")) return nullptr;

  child_read_fd_sp.release(); // make sure RAII smart pointer releases the input pipe fd before returning
  return invoke_rsp_obj;
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    invokeCommand
 * Signature: ([Ljava/lang/String;)Lspartan/Spartan/InvokeResponse;
 */
extern "C" JNIEXPORT jobject JNICALL Java_spartan_LaunchProgram_invokeCommand
    (JNIEnv *env, jclass cls, jobjectArray args) {

  const jint argc = env->GetArrayLength(args);
  if (argc > 0) {
    struct {
      jboolean isCopy;
      jstring j_str;
      const char *c_str;
    }
        first_argv_str{ JNI_FALSE, nullptr, nullptr };
    first_argv_str.j_str = (jstring) env->GetObjectArrayElement(args, 0);
    first_argv_str.c_str = env->GetStringUTFChars(first_argv_str.j_str, &first_argv_str.isCopy);
    using jstr_t = decltype(first_argv_str);
    auto const defer_cleanup_jstr = [env](jstr_t *p) {
      if (p != nullptr) {
        if (p->isCopy == JNI_TRUE) {
          env->ReleaseStringUTFChars(p->j_str, p->c_str);
        }
        env->DeleteLocalRef(p->j_str);
      }
    };
    std::unique_ptr<jstr_t, decltype(defer_cleanup_jstr)> sp_cmd(&first_argv_str, defer_cleanup_jstr);
    std::string cmd(sp_cmd->c_str);
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    sessionState shm_session;
    cmd_dsp::get_cmd_dispatch_info(shm_session);
    const auto cmds_set(cmd_dsp::get_child_processor_commands(shm_session));
    if (cmds_set.count(cmd.c_str()) <= 0) {
      throw_java_exception(env, invkcmd_excptn_cls, invoke_child_cmd_errmsg_fmt, sp_cmd->c_str);
      return nullptr;
    }
  }

  auto const check_new_obj = [env](jobject obj, const char *desc) -> bool {
    const char * const err_msg_fmt = "spawn of '%s' failed; failed allocating JNI Java object '%s'";
    return check_result(env, invkcmd_excptn_cls, obj, err_msg_fmt, progpath(), desc);
  };

  // create a Java UTF string of the program path name
  auto const progpath_utf_str = env->NewStringUTF(progpath());
  if (!check_new_obj(progpath_utf_str, "program path name UTF string")) return nullptr;

  struct {
    jstring const utf_str;
  }
      utf_str_wrpr{ progpath_utf_str };
  using utf_str_wrpr_t = decltype(utf_str_wrpr);
  auto const deref_jobjs = [env](utf_str_wrpr_t *p) {
    if (p != nullptr) {
      env->DeleteLocalRef(p->utf_str);
    }
  };
  std::unique_ptr<utf_str_wrpr_t, decltype(deref_jobjs)> sp_progpath_utf_str(&utf_str_wrpr, deref_jobjs);

  return invoke_spartan_subcommand(env, cls, sp_progpath_utf_str->utf_str, args);
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    invokeCommandEx
 * Signature: ([Ljava/lang/String;)Lspartan/Spartan/InvokeResponseEx;
 */
extern "C" JNIEXPORT jobject JNICALL Java_spartan_LaunchProgram_invokeCommandEx
    (JNIEnv */*env*/, jclass /*cls*/, jobjectArray /*args*/) {
  return nullptr;
}

static void killpid_helper(JNIEnv * const env, const pid_t pid, const int sig, const char * const sig_desc) {
  if (kill(pid, sig) == -1) {
    throw_java_exception(env, killpid_excptn_cls, killpid_failed_errmsg_fmt, pid, sig_desc, strerror(errno));
  }
}

static void killpg_helper(JNIEnv * const env, const pid_t pid, const int sig, const char * const sig_desc) {
  const pid_t pgid = getpgid(pid);
  if (pgid == -1) {
    throw_java_exception(env, killpid_excptn_cls, getpgid_failed_errmsg_fmt, pid, strerror(errno));
    return;
  }
  if (killpg(pgid, sig) == -1) {
    throw_java_exception(env, killpg_excptn_cls, killpid_failed_errmsg_fmt, pid, sig_desc, strerror(errno));
  }
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    killSIGINT
 * Signature: (I)V
 */
extern "C" JNIEXPORT void JNICALL Java_spartan_LaunchProgram_killSIGINT
    (JNIEnv *env, jclass /*cls*/, jint pid) {
  killpid_helper(env, pid, SIGINT, "SIGINT");
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    killSIGKILL
 * Signature: (I)V
 */
extern "C" JNIEXPORT void JNICALL Java_spartan_LaunchProgram_killSIGKILL
    (JNIEnv *env, jclass /*cls*/, jint pid) {
  killpid_helper(env, pid, SIGKILL, "SIGKILL");
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    killProcessGroupSIGINT
 * Signature: (I)V
 */
extern "C" JNIEXPORT void JNICALL Java_spartan_LaunchProgram_killProcessGroupSIGINT
    (JNIEnv *env, jclass /*cls*/, jint pid) {
  killpg_helper(env, pid, SIGINT, "SIGINT");
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    killProcessGroupSIGKILL
 * Signature: (I)V
 */
extern "C" JNIEXPORT void JNICALL Java_spartan_LaunchProgram_killProcessGroupSIGKILL
    (JNIEnv *env, jclass /*cls*/, jint pid) {
  killpg_helper(env, pid, SIGKILL, "SIGKILL");
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    getSysThreadID
 * Signature: ()J
 */
extern "C" JNIEXPORT jlong JNICALL Java_spartan_LaunchProgram_getSysThreadID
    (JNIEnv */*env*/, jclass /*cls*/) {
  return syscall(SYS_gettid);
}

/*
 * Class:     spartan_LaunchProgram
 * Method:    sysThreadInterrupt
 * Signature: (J)V
 */
extern "C" JNIEXPORT void JNICALL Java_spartan_LaunchProgram_sysThreadInterrupt
    (JNIEnv *env, jclass /*cls*/, jlong sysThrdId) {
  log(LL::DEBUG, ">> %s()", __func__);
  termination_flag = true;
  if (syscall(SYS_tgkill, getpid(), sysThrdId, SIGINT) == -1) {
    throw_java_exception(env, "java/lang/RuntimeException", "failed interrupting sys thread (id:%ld): %s", sysThrdId, strerror(errno));
  }
  log(LL::DEBUG, "<< %s()", __func__);
}

enum class PidFileLocation : int { VAR_RUN_DIR = 1, USER_HOME_DIR, EXE_DIR, CURR_DIR, DONE };
using PFL = PidFileLocation;

/*
 * Class:     spartan_LaunchProgram
 * Method:    isFirstInstance
 * Signature: (Ljava/lang/String;)Z
 */
extern "C" JNIEXPORT jboolean JNICALL Java_spartan_LaunchProgram_isFirstInstance
    (JNIEnv *env, jclass /*cls*/, jstring progName) {
  log(LL::DEBUG, ">> %s()", __func__);
  jboolean rtn = JNI_TRUE;
  struct {
    jboolean isCopy;
    jstring j_str;
    const char *c_str;
  }
      jstr{ static_cast<jboolean>(false), progName, nullptr };
  jstr.c_str = env->GetStringUTFChars(jstr.j_str, &jstr.isCopy);
  using jstr_t = decltype(jstr);
  auto const cleanup = [env](jstr_t *p) {
    if (p != nullptr) {
      if (p->isCopy == JNI_TRUE) {
        env->ReleaseStringUTFChars(p->j_str, p->c_str);
      }
      env->DeleteLocalRef(p->j_str);
    }
  };
  std::unique_ptr<jstr_t, decltype(cleanup)> sp_progName(&jstr, cleanup);
  std::string full_path;
  int rc = 0;
  static auto const func_name = __func__;
  const int pid_file_fd = [&full_path, &rc](const char * const prefix) -> int {
    for(PFL kind = PFL::VAR_RUN_DIR; kind < PFL::DONE; kind = static_cast<PFL>((int)kind + 1)) {
      switch (kind) {
        case PFL::VAR_RUN_DIR:
          full_path = format2str("/var/run/%s.pid", prefix);
          break;
        case PFL::USER_HOME_DIR: {
          const char *homedir = getenv("HOME");
          if (homedir == nullptr) {
            homedir = getpwuid(getuid())->pw_dir;
          }
          if (homedir == nullptr) continue;
          full_path = path_concat(homedir, prefix);
          full_path += ".pid";
          break;
        }
        case PFL::EXE_DIR:
          full_path = path_concat(progpath(), prefix);
          full_path += ".pid";
          break;
        case PFL::CURR_DIR:
          full_path = format2str("./%s.pid", prefix);
          break;
        case PFL::DONE:
          return -1;
      }
      const int fd = open(full_path.c_str(), O_CREAT | O_RDWR, 0666);
      if (fd != -1) return fd;
      rc = errno;
      if ((rc != EACCES && rc != EPERM) || static_cast<PFL>((int)kind + 1) == PFL::DONE) {
        // an error condition other than an access permission error
        log(LL::WARN, "%s() - failed open() on process pid file \"%s\":\n\t%s",
            func_name, full_path.c_str(), strerror(rc));
      }
    }
    return -1;
  }(sp_progName->c_str);
  if (pid_file_fd == -1) {
    throw_java_exception(env, invkcmd_excptn_cls, open_pidfile_failed_errmsg_fmt, full_path.c_str(), strerror(rc));
    return rtn;
  }
  rc = flock(pid_file_fd, LOCK_EX | LOCK_NB); // lock will be released when process terminates
  if (rc != 0) {
    rc = errno;
    if(rc == EWOULDBLOCK) {
      rtn = JNI_FALSE; // another instance is running
    } else {
      throw_java_exception(env, invkcmd_excptn_cls, flock_pidfile_failed_errmsg_fmt, full_path.c_str(), strerror(rc));
      return rtn;
    }
  } else {
    // this is the first instance
    int fd = dup(pid_file_fd);
    if (fd == -1) {
      log(LL::WARN, "%s() - failed dup() of process pid file \"%s\" file-descriptor:\n\t%s",
          __func__, full_path.c_str(), strerror(errno));
    } else {
      fd = pid_file_fd;
    }
    FILE * const fp = fdopen(fd, "w");
    fprintf(fp, "%d\n", getpid());
    fflush(fp);
    if (fd != pid_file_fd) {
      fclose(fp);
    }
    rtn = JNI_TRUE;
  }
  log(LL::DEBUG, "<< %s()", __func__);
  return rtn;
}