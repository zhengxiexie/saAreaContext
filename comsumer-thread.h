#ifndef __COMSUMER_THREAD_H__
#define __COMSUMER_THREAD_H__

#define CONTEXT_BUF_CACHED (100)
#define SPIN_COUNT_WAIT (100)

#include <pthread.h>
#include "reader-thread.h"

typedef struct context_thread_t_tag {
    pthread_t tid;
    pthread_mutex_t mutex;
    pthread_cond_t  pushed;
    signal_entry_t  buf[CONTEXT_BUF_CACHED];
    int read;
    int used;
} context_thread_t;

extern context_thread_t * context_thread;
extern char g_exitflag_file[256];

void * comsumer_thread(void * data);
int wait_context_thread();
int init_context_thread();
#endif /* __COMSUMER_THREAD_H__ */

