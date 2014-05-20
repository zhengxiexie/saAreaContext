#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "config.h"
#include "context.h"
#include "comsumer-thread.h"
#include "log.h"

extern volatile int exit_flag;

context_thread_t * context_thread;

void * comsumer_thread(void * th_num) {
    int thread_num = *((int*)th_num);
    context_thread_t * cthread = &context_thread[thread_num];
    signal_entry_t * se = NULL;
    int spin_count = 0;

    // init context_thread_t
    cthread->tid = pthread_self();

    logmsg(stdout, "Comsumer thread %d started", thread_num);
    while (!exit_flag || cthread->used) {
        // use mix of spinlock and mutex lock here
        if (cthread->used != 0) {
            pthread_mutex_lock(&cthread->mutex);
            se = &cthread->buf[cthread->read];
            update_context(se);
            cthread->read = (++cthread->read) % CONTEXT_BUF_CACHED;
            cthread->used--;
            pthread_mutex_unlock(&cthread->mutex);
        } else {
            spin_count++;
            if (spin_count > SPIN_COUNT_WAIT) {
                spin_count = 0;
                pthread_mutex_lock(&cthread->mutex);
                pthread_cond_wait(&cthread->pushed, &cthread->mutex);
                pthread_mutex_unlock(&cthread->mutex);
            }
        }
    }

    return 0;
}

// busy waiting all comsumer thread to empty their buffer
int wait_context_thread() {
    int notallempty = 1;
    int i = 0;

    logmsg(stdout, "Waiting all context thread");
    while (notallempty) {
        notallempty = 0;
        for (i = 0; i < CFG(context_thread); i++) {
            if (context_thread[i].used != 0) notallempty = 1;
        }
    }
    logmsg(stdout, "Waiting all context thrhread done");
    return 0;
}

int init_context_thread() {
    int i = 0;

    // start all comsumer thread;
    logmsg(stdout, "Init context thread");
    context_thread = (context_thread_t*)malloc(sizeof(context_thread_t) * CFG(context_thread));
    for (i = 0; i < CFG(context_thread); i++) {
        context_thread[i].used = 0;
        context_thread[i].read = 0;
        pthread_mutex_init(&context_thread[i].mutex, NULL);
        pthread_cond_init(&context_thread[i].pushed, NULL);
    }

    return 0;
}

