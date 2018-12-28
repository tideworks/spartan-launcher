/* echo-streams.h

Copyright 2018 Roger D. Voss

Created by roger-dv on 12/28/18.

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
#ifndef SPARTAN_ECHO_STREAMS_H
#define SPARTAN_ECHO_STREAMS_H

#include "launch-program.h"

/**
 * This function is used by Spartan client mode to handle response stream(s) processing, which
 * involves reading the input from another sub-command execution end-point and writing it to the
 * appropriate output stream(s).
 *
 * The function supports the old-style, single-response stream (e.g., Spartan.invokeCommand() API)
 * and also the react-style multi-stream scenario (e.g., Spartan.invokeCommandEx() API).
 *
 * The response stream(s) are obtained via Unix datagram socket where anonymous pipe descriptor(s)
 * are marshaled per the socket from the other end-point process into the client-mode process.
 *
 * Any error logging is done within the call context of this function so it only returns a code
 * indicating success or failure of outcome status.
 *
 * NOTE: Due to use of privately declared static variables, this function is not thread re-entrant,
 * but that is okay for this function is called only in Spartan client mode as the very last step
 * for processing response output to stdout, etc.
 *
 * @param uds_socket_name name of the Unix datagram socket where pipe file descriptors are obtained
 *        (name is supplied for any error reporting purposes)
 * @param read_fd_sp the Unix datagram socket descriptor that is read to obtain file descriptors
 * @param supervisor_pid this will be the process pid of the supervisor JVM instantiation
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int stdout_echo_response_stream(std::string const &uds_socket_name, launch_program::fd_wrapper_sp_t &&read_fd_sp,
                                const pid_t supervisor_pid = -1);

#endif //SPARTAN_ECHO_STREAMS_H