#ifndef DIE__H__
#define DIE__H__

extern const char program[];
extern const char usage[];

extern void die(int code, const char* format, ...)
#if defined(__GNUC__)
__attribute__ ((noreturn))
#endif
;
extern void die_usage(void)
#if defined(__GNUC__)
__attribute__ ((noreturn))
#endif
;
extern void warn(int sys, const char* format, ...);

#endif
