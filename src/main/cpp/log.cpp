/* log.cpp

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
#include <syslog.h>
#include <string>
#include <cassert>
#include <algorithm>
#include <functional>
#include "log.h"

namespace logger {

  const size_t DEFAULT_STRBUF_SIZE = 256;
  const char CNEWLINE = '\n';
  const char CNULLTRM = '\0';
  const LOGGING_LEVEL DEFAULT_LOGGING_LEVEL = LL::INFO;

  static volatile LOGGING_LEVEL loggingLevel = DEFAULT_LOGGING_LEVEL;
  static std::string s_progname;
  static bool s_syslogging_enabled = true;
  static std::function<void(const char*, const bool)> s_call_openlog = [](const char* const ident, bool is_enabled) {
    if (is_enabled) {
      openlog(ident, LOG_PID, LOG_DAEMON);
    }
  };
  static std::function<void(const char*, const char*)> s_syslog = [](const char* const level, const char* const msg) {
    syslog(LOG_ERR, "%s: %s", level, msg);
  };

  // NOTE: this property must be set on the logger namespace subsystem prior to use of its functions
  SO_EXPORT void set_progname(const char *const progname) {
    s_progname = std::string(progname);
    s_call_openlog(s_progname.c_str(), s_syslogging_enabled);
    s_call_openlog = [](const char* const ident, bool is_enabled) {};
  }

  extern "C" SO_EXPORT void set_syslogging(bool is_syslogging_enabled) {
    s_syslogging_enabled = is_syslogging_enabled;
    s_call_openlog(s_progname.c_str(), is_syslogging_enabled);
    s_call_openlog = [](const char* const ident, bool is_enabled) {};
    if (!is_syslogging_enabled) {
      s_syslog = [](const char*, const char*) {};
    }
  }

  extern "C" SO_EXPORT LOGGING_LEVEL get_level() { return loggingLevel; }

  // trim from start
  static inline std::string& ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
    return s;
  }

  // trim from end
  static inline std::string& rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
    return s;
  }

  // trim from both ends
  static inline std::string& trim(std::string &s) {
    return ltrim(rtrim(s));
  }

  extern "C" SO_EXPORT LOGGING_LEVEL str_to_level(const char *const logging_level) {
    std::string log_level(logging_level);
    trim(log_level);
    std::transform(log_level.begin(), log_level.end(), log_level.begin(), ::toupper);
    if (log_level.compare("TRACE") == 0) return LL::TRACE;
    if (log_level.compare("DEBUG") == 0) return LL::DEBUG;
    if (log_level.compare("INFO") == 0) return LL::INFO;
    if (log_level.compare("WARN") == 0) return LL::WARN;
    if (log_level.compare("ERR") == 0) return LL::ERR;
    if (log_level.compare("FATAL") == 0) return LL::FATAL;
    return DEFAULT_LOGGING_LEVEL; // didn't match anything so return default logging level
  }

  extern "C" SO_EXPORT void set_level(LOGGING_LEVEL level) {
    loggingLevel = level;
  }

  extern "C" SO_EXPORT void set_to_unbuffered() {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
  }

  extern "C" SO_EXPORT void vlog(LOGGING_LEVEL level, const char * const fmt, va_list ap) {
    if ((char) level < (char) loggingLevel) {
      return;
    }

    auto stream = stdout;
    const char *levelstr = ": ";
    decltype(s_syslog) syslog_it = [](const char*, const char*) {};
    const char *syslog_level = "";
    switch (level) {
      case LL::FATAL:
        levelstr = ": FATAL: ";
        stream = stderr;
        syslog_it = s_syslog;
        syslog_level = "FATAL";
        break;
      case LL::ERR:
        levelstr = ": ERROR: ";
        stream = stderr;
        syslog_it = s_syslog;
        syslog_level = "ERROR";
        break;
      case LL::WARN:
        levelstr = ": WARN: ";
        stream = stderr;
        break;
      case LL::INFO:
        levelstr = ": INFO: ";
        break;
      case LL::DEBUG:
        levelstr = ": DEBUG: ";
        break;
      case LL::TRACE:
        levelstr = ": TRACE: ";
        break;
    }

    const auto len = s_progname.size() + strlen(levelstr);
    const auto buf_extra_size = len + sizeof(CNEWLINE) + sizeof(CNULLTRM);
    int buf_size = DEFAULT_STRBUF_SIZE;
    int total_buf_size = buf_size + buf_extra_size;
    char *strbuf = (char*) alloca(total_buf_size);
    const int msgbuf_size = total_buf_size - buf_extra_size;
    int n = msgbuf_size;
    va_list parm_copy;
    va_copy(parm_copy, ap);
    {
      strcpy(strbuf, s_progname.c_str());
      strcpy(strbuf + s_progname.size(), levelstr);
      n = vsnprintf(strbuf + len, (size_t) n, fmt, ap);
      assert(n > 0);
      if (n >= msgbuf_size) {
        total_buf_size = (buf_size = ++n) + buf_extra_size;
        strbuf = (char*) alloca(total_buf_size);
        strcpy(strbuf, s_progname.c_str());
        strcpy(strbuf + s_progname.size(), levelstr);
        n = vsnprintf(strbuf + len, (size_t) n, fmt, parm_copy);
        assert(n > 0 && n < buf_size);
      }
    }
    va_end(parm_copy);
    n += len;
    strbuf[n++] = CNEWLINE;
    strbuf[n] = CNULLTRM;
    fputs(strbuf, stream);
    syslog_it(syslog_level, strbuf + len);
  }

  extern "C" SO_EXPORT void log(LOGGING_LEVEL level, const char * const fmt, ...) {
    if ((char) level < (char) loggingLevel) {
      return;
    }

    va_list ap;
    va_start(ap, fmt);
    vlog(level, fmt, ap);
    va_end(ap);
  }

  extern "C" SO_EXPORT void logm(LOGGING_LEVEL level, const char * const msg) {
    log(level, "%s", msg);
  }
}
