/* format2str.h

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
#ifndef __FORMAT2STR_H__
#define __FORMAT2STR_H__

#include <string>
#include <cstdarg>

std::string vformat2str(const char *const fmt, va_list ap);

std::string format2str(const char *const fmt, ...);

#endif //__FORMAT2STR_H__