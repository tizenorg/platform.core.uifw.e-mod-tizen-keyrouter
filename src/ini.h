/* inih -- simple .INI file parser

inih is released under the New BSD license (see LICENSE.txt). Go to the project
home page for more info:


*/

#ifndef __INI_H__
#define __INI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

int ini_parse(const char* filename,
              int (*handler)(void* user, const char* section,
                             const char* name, const char* value),
              void* user);

int ini_parse_file(FILE* file,
                   int (*handler)(void* user, const char* section,
                                  const char* name, const char* value),
                   void* user);

#ifndef INI_ALLOW_MULTILINE
#define INI_ALLOW_MULTILINE 1
#endif

#ifndef INI_ALLOW_BOM
#define INI_ALLOW_BOM 1
#endif

#ifndef INI_USE_STACK
#define INI_USE_STACK 1
#endif

#ifndef INI_STOP_ON_FIRST_ERROR
#define INI_STOP_ON_FIRST_ERROR 0
#endif

#ifndef INI_MAX_LINE
#define INI_MAX_LINE 200
#endif

#ifdef __cplusplus
}
#endif

#endif /* __INI_H__ */
