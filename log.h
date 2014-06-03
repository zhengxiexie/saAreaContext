// logging

#ifndef __LOG_H__
#define __LOG_H__

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#ifndef LOGLV
#define LOGLV 5
#endif

size_t _log(int loglv, FILE * f,
           int line, const char * file,
           const char *fmt, ...);

void print_logo( char *  );

#define logmsg(f, ...) do { \
        _log(3, f, __LINE__, __FILE__, __VA_ARGS__); \
    } while (0)

#ifdef _DEBUG
	#define logdbg(f, ...) do { \
       _log(3, f, __LINE__, __FILE__, __VA_ARGS__); \
    } while (0)
#else
	#define logdbg(f, ...)
#endif

#endif /* __LOG_H__ */
