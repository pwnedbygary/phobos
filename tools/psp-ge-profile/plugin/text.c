//psp-ge-profile's plugin: a small formatter for the dry run's report and the files' names, and the plain memory
//functions the compiler may call; the kernel's own sprintf and sysclib aren't imported, so nothing here depends on a
//NID.

#include "plugin.h"
#include <stdarg.h>

static void put(Text* text, char c) {
  if(text->length + 1 >= text->size) return;
  text->data[text->length++] = c;
  text->data[text->length] = 0;
}

static void putNumber(Text* text, u32 value, u32 base, int upper, int width, char fill, int left, int negative) {
  char digits[12];
  int n = 0;
  do {
    u32 digit = value % base;
    digits[n++] = digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + digit - 10;
    value /= base;
  } while(value);
  int length = n + negative;
  if(negative && fill == '0') put(text, '-');
  if(!left) for(; length < width; length++) put(text, fill);
  if(negative && fill != '0') put(text, '-');
  while(n) put(text, digits[--n]);
  for(; length < width; length++) put(text, ' ');
}

//%s %c %d %u %x %X and %%, each with an optional '-' (pad on the right), '0' (fill with zeros) and width. An 'l' is
//taken and ignored: u32 is unsigned long on the PSP, the same size as int, and %lu keeps GCC's format check quiet.
void textFormat(Text* text, const char* format, ...) {
  va_list args;
  va_start(args, format);
  for(const char* p = format; *p; p++) {
    if(*p != '%') {
      put(text, *p);
      continue;
    }
    p++;
    int left = 0, width = 0;
    char fill = ' ';
    if(*p == '-') left = 1, p++;
    if(*p == '0') fill = '0', p++;
    while(*p >= '0' && *p <= '9') width = width * 10 + *p++ - '0';
    if(*p == 'l') p++;
    switch(*p) {
    case 's': {
      const char* s = va_arg(args, const char*);
      if(!s) s = "";
      int length = 0;
      for(const char* q = s; *q; q++) length++;
      if(!left) for(; length < width; length++) put(text, ' ');
      while(*s) put(text, *s++);
      for(; length < width; length++) put(text, ' ');
      break;
    }
    case 'c': put(text, (char)va_arg(args, int)); break;
    case 'd': {
      int value = va_arg(args, int);
      putNumber(text, value < 0 ? -(u32)value : (u32)value, 10, 0, width, fill, left, value < 0);
      break;
    }
    case 'u': putNumber(text, va_arg(args, u32), 10, 0, width, fill, left, 0); break;
    case 'x': putNumber(text, va_arg(args, u32), 16, 0, width, fill, left, 0); break;
    case 'X': putNumber(text, va_arg(args, u32), 16, 1, width, fill, left, 0); break;
    case '%': put(text, '%'); break;
    case 0: p--; break;
    default: put(text, '%'); put(text, *p); break;
    }
  }
  va_end(args);
}

//Case doesn't matter.
int textEqual(const char* a, const char* b) {
  for(; *a && *b; a++, b++) {
    char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
    char y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
    if(x != y) return 0;
  }
  return *a == *b;
}

//On the Mac (hosttest.c), the C library's are used.
#ifndef HOST
void* memset(void* target, int value, unsigned int size) {
  unsigned char* p = target;
  while(size--) *p++ = value;
  return target;
}

void* memcpy(void* target, const void* source, unsigned int size) {
  unsigned char* p = target;
  const unsigned char* q = source;
  while(size--) *p++ = *q++;
  return target;
}
#endif
