/* send-mq-msg.h

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
#ifndef __SEND_MQ_MSG_H__
#define __SEND_MQ_MSG_H__

#include <functional>

namespace send_mq_msg {
  // NOTE: this property must be set on the send_mq_msg namespace subsystem prior to use of its functions
  void set_progname(const char *const progname);

  extern "C" {
    // wraps call to OS API of same name - sets umask prior to call and then restores umask
    mqd_t mq_open_ex(const char *name, int oflag, mode_t mode, struct mq_attr *attr);

    // The core function for sending a message to a specified mq queue; does
    // appropriate return code error checking, prints errors to stderr output
    // if detected, returns EXIT_SUCCESS on success or otherwise EXIT_FAILURE.
    int send_mq_msg(const char *const msg, const char *const queue_name);

    typedef std::function<void(int&,char*[])> str_array_filter_cb_t;

    // Put the argv args into a flattened string - double quote each arg then
    // send as a message to the parent supervisor process. If the fifo_pipe_name
    // parameter is not null, then it becomes the first argv argument (index zero).
    int send_flattened_argv_mq_msg(int argc, char **argv, const char *const fifo_pipe_name,
                                             const char *const queue_name, str_array_filter_cb_t filter);
  }
}
#endif //__SEND_MQ_MSG_H__
