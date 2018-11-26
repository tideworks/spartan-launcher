/* mq-queue.cpp

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

#include "format2str.h"
#include "mq-queue.h"

static const char JLAUNCHER_QUEUE_NAME[]   = "/%s_JLauncher";
static const char JSUPERVISOR_QUEUE_NAME[] = "/%s_JSupervisor";

static std::string get_mq_queue_name(const char * const name_tmpl, const char * const progname) {
  return format2str(name_tmpl, progname);
}

std::string get_jlauncher_mq_queue_name(const char * const progname) {
  return get_mq_queue_name(JLAUNCHER_QUEUE_NAME, progname);
}

std::string get_jsupervisor_mq_queue_name(const char * const progname) {
  return get_mq_queue_name(JSUPERVISOR_QUEUE_NAME, progname);
}
