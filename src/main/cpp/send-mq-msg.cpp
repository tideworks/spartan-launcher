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
#include <sys/stat.h>
#include "log.h"
#include "send-mq-msg.h"

//#undef NDEBUG // uncomment this line to enable asserts in use below
#include <cassert>

// Need to also use gcc/g++ -rdynamic compiler option when enabling debug stack trace.
// (0 to disable : non-zero to enable)
#define DBG_STK_TRC 0
#ifdef DBG_STK_TRC
#include <execinfo.h>
#endif

using namespace logger;

namespace send_mq_msg {
  /**
   * Wraps call to OS API of mq_open() - sets umask prior to call and then restores umask.
   *
   * Establishes connection between a process and a message queue NAME and returns message
   * queue descriptor or (mqd_t) -1 on error.
   *
   * OFLAG determines the type of access used.
   *
   * If O_CREAT is on OFLAG, the third argument is taken as a 'mode_t' - the mode of the
   * created message queue.
   *
   * The fourth argument is taken as 'struct mq_attr *', pointer to message queue attributes.
   *
   * If the fourth argument is NULL, default attributes are used.
   *
   * @param name name of the queue to open
   * @param oflag access flags
   * @param mode mode of the queue to be opened
   * @param attr pointer to structure for attributes for the opened queue (may be null to accept defaults)
   * @return queue descriptor or -1 on error
   */
  mqd_t mq_open_ex(const char* const name, int oflag, mode_t mode, struct mq_attr *attr) {
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

  /**
   * The core function for sending a message to a specified mq queue; does appropriate
   * return code error checking, prints errors to stderr output if detected, returns
   * EXIT_SUCCESS on success or otherwise EXIT_FAILURE.
   *
   * @param msg message text to be sent
   * @param queue_name name of target queue to publish to
   * @return a result of zero indicates message was successfully published to target queue
   */
  int send_mq_msg(string_view const msg, string_view const queue_name) {
    log(LL::DEBUG, "%s() called:\n\tmsg: %s\n\tque: %s", __func__, msg.c_str(), queue_name.c_str());
    struct {
      const mqd_t mqd;
    }
        wrp_mqd = { send_mq_msg::mq_open_ex(queue_name.c_str(), O_WRONLY, 0662, nullptr) };
    using wrp_mqd_t = decltype(wrp_mqd);
    if (wrp_mqd.mqd == -1) {
      const auto rc = errno;
      log(LL::ERR, "mq_open_ex(\"%s\") failed: %s; (try starting service first)", queue_name.c_str(), strerror(rc));
#if DBG_STK_TRC
      if (rc == EMFILE) {
        show_stackframe(EXIT_FAILURE);
      }
#endif
      return EXIT_FAILURE;
    }
    auto const close_mqd = [](wrp_mqd_t *p) {
      if (p != nullptr) {
        mq_close(p->mqd);
      }
    };
    std::unique_ptr<decltype(wrp_mqd), decltype(close_mqd)> mqd_sp(&wrp_mqd, close_mqd);
    if (mq_send(mqd_sp->mqd, msg.c_str(), msg.size(), 0) != 0) {
      log(LL::ERR, "mq_send() on queue \"%s\" failed: %s", queue_name.c_str(), strerror(errno));
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  /**
   * Put the argv args into a flattened string - double quote each arg then send
   * as a message to a target queue (supervisor or child process launcher).
   *
   * The extended_invoke_cmd parameter becomes the second argv argument (index zero).
   *
   * The uds_socket_name parameter becomes the first argv argument (index one).
   *
   * @param argc number of string arguments in argv array (not counting last null entry)
   * @param argv array of string arguments - the last entry in the array is a null entry, indexed directly by argc
   * @param extended_invoke_cmd a command option that informs (true or false) if is an extended style of invoke command
   * @param uds_socket_name name of the unix datagram to be used to marshal anonymous pipe(s) back to the caller
   * @param queue_name name of the message queue to publish flattened string to (supervisor or child)
   * @param filter a call-back that can filter out any command line arguments that should not be present
   * @return a result of zero indicates the flattened string was successfully published to the named queue
   */
  int send_flattened_argv_mq_msg(int argc, char **argv, string_view const extended_invoke_cmd,
                                 string_view const uds_socket_name, string_view const queue_name,
                                 str_array_filter_cb_t filter)
  {
    assert(argc >= 1); // there's always the program path as first argument in argv[]
    assert(!extended_invoke_cmd.empty());
    assert(!uds_socket_name.empty());

    // duplicate the argv array into temp stack memory array, argv_dup
    auto const argv_dup = (char**) alloca((++argc + 1) * sizeof(argv[0]));
    argv_dup[argc] = nullptr; // argv convention is that there is a nullptr sentinel element at end of the array
    argv_dup[0] = const_cast<char*>(extended_invoke_cmd.c_str());
    argv_dup[1] = const_cast<char*>(uds_socket_name.c_str());
    for (int i = 2; i < argc; i++) {
      argv_dup[i] = argv[i - 1];
    }

    filter(argc, argv_dup); // invoke callback filter to remove unwanted argv arguments

    // calculate a buffer to hold the flattened out string of argv arguments
    const auto quote_overhead = strlen("\"\" ");
    static const auto quote_ch = '"';
    static const auto space_ch = ' ';
    static const auto null_ch = '\0';
    auto const argv_sizes = (size_t*) alloca(argc * sizeof(size_t));
    argv_sizes[0] = extended_invoke_cmd.size();
    argv_sizes[1] = uds_socket_name.size();
    size_t buf_size = (argv_sizes[0] + argv_sizes[1] + quote_overhead * 2);
    for (int i = 2; i < argc; i++) {
      const auto arg_str_len = argv_sizes[i] = strlen(argv_dup[i]);
      buf_size += (arg_str_len + quote_overhead);
    }

    // stack allocate buffer for holding the flattened out string of argv arguments
    auto const buf = (char*) alloca(++buf_size);
    buf[0] = null_ch;  // null terminate buffer starting out as an empty C string
    auto bufpos = buf; // initialize buf ptr to be used for pointer arithmetic

    // now run through the argv_dup array, copy each arg into string buffer and put
    // double quotes around it; a space character delimiter separates each argument
    size_t sum = 0;
    for (int i = 0; i < argc; i++) {
      const auto n = argv_sizes[i];
      if (n == 0) continue; // skip empty string argv arguments
      assert(buf_size >= (sum + n + quote_overhead));
      *bufpos++ = quote_ch;
      strncpy(bufpos, argv_dup[i], n);
      bufpos += n;
      *bufpos++ = quote_ch;
      *bufpos++ = space_ch;
      sum += (n + quote_overhead);
    }
    *(--bufpos) = null_ch; // replace last space char with null C string termination character
    assert((bufpos - buf) == static_cast<long int>(sum - 1));
    assert(buf_size >= sum);

    log(LL::DEBUG, "%s(): inform service at queue \'%s\' to process:\n\t\'%s\'", __func__, queue_name.c_str(), buf);
    // now send the string buffer to the parent supervisor process
    return send_mq_msg::send_mq_msg({buf, sum - 1}, queue_name);
  }
} // namespace send_mq_msg