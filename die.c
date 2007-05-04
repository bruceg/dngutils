#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "die.h"

static void msg(const char* prefix, int sys, const char* format, va_list ap)
{
  fputs(program, stderr);
  fputs(prefix, stderr);
  vfprintf(stderr, format, ap);
  if (sys) {
    fputs(": ", stderr);
    fputs(strerror(errno), stderr);
  }
  fputc('\n', stderr);
  fflush(stderr);
}

void warn(int sys, const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  msg(": Fatal error: ", sys, format, ap);
  va_end(ap);
}

void die(int code, const char* format, ...)
{
  va_list ap;

  va_start(ap, format);
  msg(": Fatal error: ", code < 0, format, ap);
  va_end(ap);
  exit(abs(code));
}

void die_usage(void)
{
  fputs(usage, stderr);
  exit(1);
}
