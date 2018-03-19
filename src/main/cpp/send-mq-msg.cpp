/* send-mq-msg.cpp

Copyright 2015 - 2016 Tideworks Technology
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
#include <string>
#include <mqueue.h>
#include <cstring>
#include <memory>
#include <cassert>
#include <sys/stat.h>
#include "log.h"
#include "send-mq-msg.h"

// Need to also use gcc/g++ -rdynamic compiler option when enabling debug stack trace.
// (0 to disable : non-zero to enable)
#define DBG_STK_TRC 0
#ifdef DBG_STK_TRC
#include <execinfo.h>
#endif

using logger::log;
using logger::LL;

namespace send_mq_msg {
  static std::string s_progname;
  inline const char *progname() { return s_progname.c_str(); }

  // NOTE: this property must be set on the send_mq_msg namespace subsystem prior to use of its functions
  SO_EXPORT void set_progname(const char *const progname) {
    s_progname = std::string(progname);
  }

  // wraps call to OS API of same name - sets umask prior to call and then restores umask
  SO_EXPORT mqd_t mq_open(const char *name, int oflag, mode_t mode, struct mq_attr *attr) {
    const mode_t save_umask = umask(002); // coerce a default umask value for this call
    const mqd_t mqd = ::mq_open(name, oflag, mode, attr); // call the OS API now
    umask(save_umask);
    return mqd;
  }

#if DBG_STK_TRC
  static void show_stackframe(int status) {
    void * trace[16];
    const auto trace_size = backtrace(trace, 16);
    char * const * messages = backtrace_symbols(trace, trace_size);
    printf("[bt] Execution path:\n");
    for (int i =0 ; i < trace_size; ++i) {
      printf("[bt] %s\n", messages[i]);
    }
    exit(status);
  }
#endif

  // The core function for sending a message to a specified mq queue; does
  // appropriate return code error checking, prints errors to stderr output
  // if detected, returns EXIT_SUCCESS on success or otherwise EXIT_FAILURE.
  SO_EXPORT int send_mq_msg(const char *const msg, const char *const queue_name) {
    log(LL::DEBUG, "%s() called:\n\tmsg: %s\n\tque: %s", __func__, msg, queue_name);
    struct {
      const mqd_t mqd;
    }
        wrp_mqd = {send_mq_msg::mq_open(queue_name, O_WRONLY, 0662, NULL)};
    if (wrp_mqd.mqd == -1) {
      const auto rc = errno;
      log(LL::ERR, "mq_open(\"%s\") failed: %s; (try starting service first)", queue_name, strerror(rc));
#if DBG_STK_TRC
      if (rc == EMFILE) {
        show_stackframe(EXIT_FAILURE);
      }
#endif
      return EXIT_FAILURE;
    }
    auto const close_mqd = [](decltype(wrp_mqd) *p) {
      if (p != nullptr) {
        mq_close(p->mqd);
      }
    };
    std::unique_ptr<decltype(wrp_mqd), decltype(close_mqd)> mqd_sp(&wrp_mqd, close_mqd);
    if (mq_send(mqd_sp->mqd, msg, strlen(msg), 0) != 0) {
      log(LL::ERR, "mq_send() on queue \"%s\" failed: %s", queue_name, strerror(errno));
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

// Put the argv args into a flattened string - double quote each arg then
// send as a message to the parent supervisor process. If the fifo_pipe_name
// parameter is not null, then it becomes the first argv argument (index zero).
  SO_EXPORT int send_flattened_argv_mq_msg(int argc, char **argv, const char *const fifo_pipe_name,
                                           const char *const queue_name, str_array_filter_cb_t filter) {
    // duplicate the argv array into temp stack memory array, argv_dup
    char **const argv_dup = (char **) alloca((argc + 1) * sizeof(argv[0]));
    argv_dup[argc] = nullptr; // argv convention is that there is a nullptr sentinel element at end of the array
    argv_dup[0] = const_cast<char *>(fifo_pipe_name);
    for (int i = 1; i < argc; i++) {
      argv_dup[i] = argv[i];
    }
    filter(argc, argv_dup);
    const int start_index = argv_dup[0] != nullptr ? 0 : 1;
    const auto quote_overhead = strlen("\"\" ");
    static const auto quote_ch = '"';
    static const auto space_ch = ' ';
    static const auto null_ch = '\0';
    size_t *const argv_sizes = (size_t *) alloca(argc * sizeof(size_t));
    argv_sizes[0] = argv_dup[0] != nullptr ? strlen(argv_dup[0]) : 0;
    size_t buf_size = 0;
    for (int i = start_index; i < argc; i++) {
      buf_size += ((argv_sizes[i] = strlen(argv_dup[i])) + quote_overhead);
    }
    char *const buf = (char *) alloca(++buf_size);
    buf[0] = null_ch; // null terminate buffer
    char *bufpos = buf;
    // now run through the argv_dup array, copy each arg
    // into string buffer and put double quotes around it;
    // a space character delimiter separates each argument
    size_t sum = 0;
    for (int i = start_index; i < argc; i++) {
      const auto n = argv_sizes[i];
      assert(buf_size >= sum + n + quote_overhead);
      *bufpos++ = quote_ch;
      strncpy(bufpos, argv_dup[i], n);
      bufpos += n;
      *bufpos++ = quote_ch;
      *bufpos++ = space_ch;
      sum += (n + quote_overhead);
    }
    *(--bufpos) = null_ch; // replace last space char with null
    log(LL::DEBUG, "%s(): inform service to process:\n\t\'%s\'", __func__, buf);
    // now send the string buffer to the parent supervisor process
    return send_mq_msg::send_mq_msg(buf, queue_name);
  }
}
