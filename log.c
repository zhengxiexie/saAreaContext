// Log utils

#include "log.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pthread.h>

size_t _log(int loglv, FILE * f,
           int line, const char * file,
           const char * fmt, ...) {

    size_t  ret = 0;
    va_list args;
    char    tmp_log[1024];
    char    tmp_ts[64];
    time_t  tmp_time;

	pthread_t pthread_id = pthread_self();

    if (loglv > LOGLV) return 0;

    tmp_time = time(NULL);
    strftime(tmp_ts, 27, "%Y-%m-%d %H:%M:%S", localtime(&tmp_time));

    memset(tmp_log, 0, sizeof(tmp_log));
    va_start(args, fmt);
    vsnprintf(tmp_log, 1023, fmt, args);
    va_end(args);

    ret = fprintf(f, "[%s][%-15s:%4d][tid:%15lu]: %s\n", tmp_ts, file, line, pthread_id, tmp_log);
    return ret;
}
