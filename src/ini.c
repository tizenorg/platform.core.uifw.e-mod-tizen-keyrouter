/* inih -- simple .INI file parser

inih is released under the New BSD license (see LICENSE.txt). Go to the project
home page for more info:


*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "ini.h"
#include "errno.h"

#if !INI_USE_STACK
#include <stdlib.h>
#endif

#define MAX_SECTION 50
#define MAX_NAME 50

static char *
rstrip(char* s)
{
   char* p = s + strlen(s);
   while (p > s && isspace((unsigned char)(*--p)))
     *p = '\0';
   return s;
}

static char *
lskip(char* s)
{
   while (*s && isspace((unsigned char)(*s)))
     s++;
   return (char*)s;
}

static char *
find_char_or_comment(char* s, char c)
{
   int was_whitespace = 0;
   while (*s && *s != c && !(was_whitespace && *s == ';'))
     {
        was_whitespace = isspace((unsigned char)(*s));
        s++;
     }
   return (char*)s;
}

static char *
strncpy0(char* dest, const char* src, size_t size)
{
   strncpy(dest, src, size);
   dest[size - 1] = '\0';
   return dest;
}

int
ini_parse_file(FILE* file,
               int (*handler)(void*, const char*, const char*, const char*),
               void* user)
{
#if INI_USE_STACK
   char line[INI_MAX_LINE];
#else
   char* line;
#endif
   char section[MAX_SECTION] = "";
   char prev_name[MAX_NAME] = "";

   char* start;
   char* end;
   char* name;
   char* value;
   int lineno = 0;
   int error = 0;

#if !INI_USE_STACK
   line = (char*)malloc(INI_MAX_LINE);
   if (!line)
     {
        return -2;
     }
#endif

   while (fgets(line, INI_MAX_LINE, file) != NULL)
     {
        lineno++;

        start = line;
#if INI_ALLOW_BOM
        if (lineno == 1 && (unsigned char)start[0] == 0xEF &&
           (unsigned char)start[1] == 0xBB &&
           (unsigned char)start[2] == 0xBF)
          {
             start += 3;
          }
#endif
        start = lskip(rstrip(start));

        if (*start == ';' || *start == '#')
          {
             ;
          }
#if INI_ALLOW_MULTILINE
        else if (*prev_name && *start && start > line)
          {
             if (!handler(user, section, prev_name, start) && !error)
               error = lineno;
          }
#endif
        else if (*start == '[')
          {
             end = find_char_or_comment(start + 1, ']');
             if (*end == ']')
               {
                  *end = '\0';
                  strncpy0(section, start + 1, sizeof(section));
                  *prev_name = '\0';
               }
             else if (!error)
               {
                  error = lineno;
               }
          }
        else if (*start && *start != ';')
          {
             end = find_char_or_comment(start, '=');
             if (*end != '=')
               {
                  end = find_char_or_comment(start, ':');
               }
             if (*end == '=' || *end == ':')
               {
                  *end = '\0';
                  name = rstrip(start);
                  value = lskip(end + 1);
                  end = find_char_or_comment(value, '\0');

                  if (*end == ';')
                    *end = '\0';
                  rstrip(value);

                  strncpy0(prev_name, name, sizeof(prev_name));

                  if (!handler(user, section, name, value) && !error)
                    error = lineno;
               }
             else if (!error)
               {
                  error = lineno;
               }
          }

#if INI_STOP_ON_FIRST_ERROR
        if (error)
          break;
#endif
     }

#if !INI_USE_STACK
   free(line);
#endif

   return error;
}


int
ini_parse(const char* filename,
          int (*handler)(void*, const char*, const char*, const char*),
          void* user)
{
   FILE* file;
   int error;

   file = fopen(filename, "r");
   if (!file)
     return -1;
   error = ini_parse_file(file, handler, user);
   fclose(file);
   return error;
}
