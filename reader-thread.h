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

typedef struct signal_entry_t_tag {
    char  imsi[16];
    time_t timestamp;
    char lac_cell[12];
    uint16_t event;
} signal_entry_t;

typedef struct context_sort_buffer_t_tag {
    int    size;
    int    used;
    time_t time;
    signal_entry_t * buffer;
} context_sort_buffer_t;

void * read_file_thread(void * data);

#endif
