#ifndef FMT_H
#define FMT_H

#include <stdarg.h>
#include <stdint.h>

// %s %c %d %u %x %X %%, with '-' and '0' flags and a width

int fmt_vformat(char* output, int size, const char* format, va_list arguments);
int fmt_format(char* output, int size, const char* format, ...);
int fmt_length(const char* text);
void fmt_copy(char* output, const char* text, int size);

#endif /* FMT_H */
