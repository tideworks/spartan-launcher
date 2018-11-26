/* log.h

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
#ifndef __LOG_H__
#define __LOG_H__

#include <cstdarg>

namespace logger {

  // logging levels
  enum class LOGGING_LEVEL : char {
    FATAL = 6,
    ERR = 5,
    WARN  = 4,
    INFO  = 3,
    DEBUG = 2,
    TRACE = 1
  };
  using LL = LOGGING_LEVEL;

  // NOTE: this property must be set on the logger namespace subsystem prior to use of its functions
  void set_progname(const char *const progname);

  void set_syslogging(bool is_syslogging_enabled);
  LOGGING_LEVEL get_level();
  inline bool is_debug_level() { return get_level() == LL::DEBUG; }
  inline bool is_trace_level() { return get_level() == LL::TRACE; }
  LOGGING_LEVEL str_to_level(const char *const logging_level);
  void set_level(LOGGING_LEVEL level);
  void set_to_unbuffered();
  void vlog(LOGGING_LEVEL level, const char * const fmt, va_list ap);
  void log(LOGGING_LEVEL level, const char * const fmt, ...);
  void logm(LOGGING_LEVEL level, const char * const msg);

}

#endif //__LOG_H__