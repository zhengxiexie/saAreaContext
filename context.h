#ifndef __CONTEXT_H__
#define __CONTEXT_H__
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "reader-thread.h"

#define CONTEXT_ERR_MALLOC 1
#define APP_NAME "saAreaConext"

// area info
struct area_t {
    char     area_id[8];
    uint8_t  mobile_open_counts;
    uint8_t  mobile_close_counts;
    uint8_t  smo_sms_counts;
    uint8_t  smt_sms_counts;
    uint8_t  calling_call_counts;
    uint8_t  called_call_counts;
    uint16_t resident_time;
    time_t   last_event_time;
    time_t   come_time;
    time_t   leave_time;
    time_t   last_open_time;
    time_t   last_close_time;
    struct area_t * next;
};
typedef struct area_t area_t;

// user information in hash table
struct context_content_t {
    char     imsi[16];
    uint16_t num_of_area;
    area_t * areas;
    struct context_content_t * next;
};
typedef struct context_content_t context_content_t;

// each segment in context
struct context_seg_t {
    uint64_t size;
   context_content_t * content;
    pthread_mutex_t mutex_lock;
};
typedef struct context_seg_t context_seg_t;

// the context
struct context_t {
    uint64_t size;
    uint32_t part;
    context_seg_t contexts[CONTEXT_PART];
};
typedef struct context_t context_t;

extern context_t context;
extern int context_inited;

int update_context(signal_entry_t * content);
int parse_line(const char * line, signal_entry_t * se);
int check_hourly_update(time_t t);
int dump_context(const char * filename);
int restore_context(const char * filename);
int init_context();
int daily_cleanup(time_t t);
int read_cell_map(const char * filename);

#endif /* __CONTEXT_H__ */
