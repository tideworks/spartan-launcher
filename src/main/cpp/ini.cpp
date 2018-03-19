/* inih -- simple .INI file parser

inih is released under the New BSD license (see BSD-LICENSE.txt). Go to the project
home page for more info:

http://code.google.com/p/inih/

Copyright (c) 2009, Ben Hoyt
All rights reserved.

inih library license: https://raw.githubusercontent.com/benhoyt/inih/master/LICENSE.txt

Enhanced for use of C++11 syntax and features; May 2015, Roger D. Voss

*/
#include <cctype>
#include <cstring>
#include <cerrno>
#include <memory>
#include "ini.h"


#define MAX_SECTION 50
#define MAX_NAME 50

/* Strip whitespace chars off end of given string, in place. Return s. */
static char* rstrip(char* s)
{
    char* p = s + strlen(s);
    while (p > s && isspace((unsigned char)(*--p))) {
        *p = '\0';
    }
    return s;
}

/* Return pointer to first non-whitespace char in given string. */
static char* lskip(const char* s)
{
    while (*s && isspace((unsigned char)(*s))) {
        s++;
    }
    return const_cast<char*>(s);
}

/* Return pointer to first char c or ';' comment in given string, or pointer to
   null at end of string if neither found. ';' must be prefixed by a whitespace
   character to register as a comment. */
static char* find_char_or_comment(const char* s, char c)
{
    int was_whitespace = 0;
    while (*s && *s != c && !(was_whitespace && *s == ';')) {
        was_whitespace = isspace((unsigned char)(*s));
        s++;
    }
    return const_cast<char*>(s);
}

/* Version of strncpy that ensures dest (size bytes) is null-terminated. */
static char* strncpy0(char* dest, unsigned int dest_buf_size, const char* src, size_t size,
                      std::function<void (int ec,const char* op,int ln)> &error_code)
{
    if (size >= dest_buf_size)
      size = dest_buf_size - 1;
    return strncpy(dest, src, size);
}

/* See documentation in header file. */
static int ini_parse_file_core(FILE* file, cfg_parse_handler_t &handler, err_code_handler_t &error_code)
{
    /* Uses a fair bit of stack (use heap instead if you need to) */
    char line[INI_MAX_LINE];
    char section[MAX_SECTION] = "";
    char prev_name[MAX_NAME] = "";

    char* start = nullptr;
    char* end   = nullptr;
    char* name  = nullptr;
    char* value = nullptr;
    int lineno = 0;
    int error = 0;



    /* Scan through file line by line */
    while (fgets(line, INI_MAX_LINE, file) != nullptr) {
        lineno++;

        start = line;
#if INI_ALLOW_BOM
        if (lineno == 1 && (unsigned char)start[0] == 0xEF &&
                           (unsigned char)start[1] == 0xBB &&
                           (unsigned char)start[2] == 0xBF) {
            start += 3;
        }
#endif
        start = lskip(rstrip(start));

        if (*start == ';' || *start == '#') {
            /* Per Python ConfigParser, allow '#' comments at start of line */
        }
#if INI_ALLOW_MULTILINE
        else if (*prev_name && *start && start > line) {
            /* Non-black line with leading whitespace, treat as continuation
               of previous name's value (as per Python ConfigParser). */
            if (!handler(section, prev_name, start) && !error) {
                error = lineno;
            }
            else {
              error_code(EILSEQ, __func__, __LINE__);
            }
        }
#endif
        else if (*start == '[') {
            /* A "[section]" line */
            end = find_char_or_comment(start + 1, ']');
            if (*end == ']') {
                *end = '\0';
                strncpy0(section, MAX_SECTION, start + 1, sizeof(section), error_code);
                *prev_name = '\0';
            }
            else if (!error) {
                /* No ']' found on section line */
                error = lineno;
            }
            else {
              error_code(EILSEQ, __func__, __LINE__);
            }
        }
        else if (*start && *start != ';') {
            /* Not a comment, must be a name[=:]value pair */
            end = find_char_or_comment(start, '=');
            if (*end != '=') {
                end = find_char_or_comment(start, ':');
            }
            if (*end == '=' || *end == ':') {
                *end = '\0';
                name = rstrip(start);
                value = lskip(end + 1);
                end = find_char_or_comment(value, '\0');
                if (*end == ';')
                    *end = '\0';
                rstrip(value);

                /* Valid name[=:]value pair found, call handler */
                strncpy0(prev_name, MAX_NAME, name, sizeof(prev_name), error_code);
                if (!handler(section, name, value) && !error)
                    error = lineno;
            }
            else if (!error) {
                /* No '=' or ':' found on name[=:]value line */
                error = lineno;
            }
            else {
              error_code(EILSEQ, __func__, __LINE__);
            }
        }

#if INI_STOP_ON_FIRST_ERROR
        if (error)
            break;
#endif
    }

    return error;
}

/* See documentation in header file. */
int ini_parse(FILE* file, cfg_parse_handler_t handler, err_code_handler_t error_code)
{
  return ini_parse_file_core(file, handler, error_code);
}

int ini_parse(const char* filename, cfg_parse_handler_t handler, err_code_handler_t error_code)
{
    int error = 0;
    auto const close_file = [&error](FILE *f) {
      if (f != nullptr) {
        if (fclose(f) != 0)
          error = errno;
      }
    };
    FILE* const file = fopen(filename, "r");
    if (file == nullptr) {
        error_code(errno, __func__, __LINE__); // open file error
        return -1;
    }
    std::unique_ptr<FILE, decltype(close_file)> file_sp(file, close_file);
    const int err = ini_parse_file_core(file, handler, error_code);
    file_sp.reset(nullptr);
    if (err != 0) {
        error_code(err, __func__, __LINE__);   // parse file error
        return -1;
    } else if (error != 0) {
        error_code(error, __func__, __LINE__); // close file error
        return -1;
    }
    return 0;
}
