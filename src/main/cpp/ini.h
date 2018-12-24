/* inih -- simple .INI file parser

inih is released under the New BSD license (see BSD-LICENSE.txt). Go to the project
home page for more info:

http://code.google.com/p/inih/

Copyright (c) 2009, Ben Hoyt
All rights reserved.

inih library license: https://raw.githubusercontent.com/benhoyt/inih/master/LICENSE.txt

Enhanced for use of C++11 syntax and features; May 2015, Roger D. Voss
clang-tidy inspection recommendations applied; Dec 2018, Roger D. Voss

*/
#ifndef __INI_H__
#define __INI_H__

#include <functional>
#include <cstdio>

using cfg_parse_handler_t = std::function<int (const char*, const char*, const char*)>;
using err_code_handler_t = std::function<void (int ec, const char* op, int ln)>;

/* Parse given INI-style file. May have [section]s, name=value pairs
   (whitespace stripped), and comments starting with ';' (semicolon). Section
   is "" if name=value pair parsed before any section heading. name:value
   pairs are also supported as a concession to Python's ConfigParser.

   For each name=value pair parsed, call handler function with given user
   pointer as well as section, name, and value (data only valid for duration
   of handler call). Handler should return nonzero on success, zero on error.

   Returns 0 on success, line number of first error on parse error (doesn't
   stop on first error), -1 on file open error, or -2 on memory allocation
   error (only when INI_USE_STACK is zero).
*/
int ini_parse(const char *const filename, const cfg_parse_handler_t &handler, const err_code_handler_t &error_code);

/* Same as ini_parse(), but takes a FILE* instead of filename. This doesn't
   close the file when it's finished -- the caller must do that. */
int ini_parse_file(FILE *const file, const cfg_parse_handler_t &handler, const err_code_handler_t &error_code);

/* Nonzero to allow multi-line value parsing, in the style of Python's
   ConfigParser. If allowed, ini_parse() will call the handler with the same
   name for each subsequent line parsed. */
#ifndef INI_ALLOW_MULTILINE
#define INI_ALLOW_MULTILINE 0
#endif

/* Nonzero to allow a UTF-8 BOM sequence (0xEF 0xBB 0xBF) at the start of
   the file. See http://code.google.com/p/inih/issues/detail?id=21 */
#ifndef INI_ALLOW_BOM
#define INI_ALLOW_BOM 1
#endif

/* Stop parsing on first error (default is to keep parsing). */
#ifndef INI_STOP_ON_FIRST_ERROR
#define INI_STOP_ON_FIRST_ERROR 1
#endif

/* Maximum line length for any line in INI file. */
#ifndef INI_MAX_LINE
#define INI_MAX_LINE 512
#endif

#endif /* __INI_H__ */