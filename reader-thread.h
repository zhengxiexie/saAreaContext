#ifndef __READER_THREAD_H__
#define __READER_THREAD_H__

extern volatile int exit_flag;

#define MAX_LINE (1024)
#define MIN(a, b) ((a) > (b)? (b): (a))
#include <pthread.h>
#include <time.h>
#include <stdint.h>

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <malloc.h>

struct signal_entry_t {
    char  imsi[16];
    time_t timestamp;
    char lac_cell[12];
    uint16_t event;
};
typedef struct signal_entry_t signal_entry_t;

struct context_sort_buffer_t {
    int    size;
    int    used;
    time_t time;
    signal_entry_t * buffer;
};
typedef struct context_sort_buffer_t context_sort_buffer_t;

void * read_file_thread(void * data);

#endif /* __READER_THREAD_H__ */

