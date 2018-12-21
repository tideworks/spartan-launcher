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

#include "string-view.h"
#include <functional>

namespace send_mq_msg {

  using bpstd::string_view;

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
  mqd_t mq_open_ex(const char* const name, int oflag, mode_t mode, struct mq_attr *attr);

  /**
   * The core function for sending a message to a specified mq queue; does appropriate
   * return code error checking, prints errors to stderr output if detected, returns
   * EXIT_SUCCESS on success or otherwise EXIT_FAILURE.
   *
   * @param msg message text to be sent
   * @param queue_name name of target queue to publish to
   * @return a result of zero indicates message was successfully published to target queue
   */
  int send_mq_msg(string_view const msg, string_view const queue_name);

  typedef std::function<void(int&,char*[])> str_array_filter_cb_t;

  /**
   * Put the argv args into a flattened string - double quote each arg then send
   * as a message to the parent supervisor process.
   *
   * If the uds_socket_name parameter is not null, then it becomes the first argv
   * argument (index zero).
   * @param argc number of string arguments in argv array (not counting last null entry)
   * @param argv array of string arguments - the last entry in the array is a null entry
   * @param uds_socket_name name of the unix datagram to be used to marshal anonymous pipe(s) over
   * @param extended_invoke_cmd a command option that informs (true or false) if is an extended style of invoke command
   * @param queue_name name of the message queue to publish flattened string to (supervisor or child)
   * @param filter a call-back that can filter out any command line arguments that should not be present
   * @return a result of zero indicates the flattened string was successfully published to the named queue
   */
  int send_flattened_argv_mq_msg(int argc, char **argv, string_view const uds_socket_name,
                                 string_view const extended_invoke_cmd, string_view const queue_name,
                                 str_array_filter_cb_t filter);
}
#endif //__SEND_MQ_MSG_H__