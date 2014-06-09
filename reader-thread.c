#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <malloc.h>
#include <signal.h>

#include "context.h"
#include "config.h"
#include "log.h"
#include "reader-thread.h"
#include "comsumer-thread.h"

typedef struct file_info_t_tag
{
    char filename[256];
} file_info_t;

extern context_sort_buffer_t * context_sort_buf;
extern pthread_attr_t attr;
volatile int exit_flag = 0;

time_t signal_current = -1;

char g_exitflag_file[256];
int need_daily_cleanup = 1;

static int need_update_area_cell_map = 0;

static int push_to_sort_buf(signal_entry_t * se);
static int push_to_context(context_sort_buffer_t * csb);
static char * read_line(FILE * f, char * line, size_t byte);
static int write_back_format(const signal_entry_t * c, char * line);
static int check_exit_flag();
static int write_back_context();
static int init_sort_buffer();
static int time_format(const time_t t, char * line);
static void sigusr1_handler(int signo);

static int file_list_comp(const void * f1, const void * f2)
{
    return strcmp(((file_info_t*)f1)->filename, ((file_info_t*)f2)->filename);
}

static int comp_signal_func(const void * se1, const void * se2)
{
    return ((signal_entry_t*)se1)->timestamp - ((signal_entry_t*)se2)->timestamp;
}

void * read_file_thread(void * data)
{
    int  i = 0;
    int  num_of_file = 0;
    int  max_num_file = 0;

    DIR  * dp;
    pthread_t * cthread;

    struct stat file_stat;
    struct dirent * entry;

    file_info_t * file_list;
    init_sort_buffer();
    int * ts = NULL;

    char backup_filename[256];
    sprintf(backup_filename, "%s/" APP_NAME "_exit.bkp", CFG(backup_dir));

    // init context_thread
    init_context_thread();

    // load backup file
    restore_context(backup_filename);

    // start all context thread
    cthread = (pthread_t*)calloc(CFG(context_thread), sizeof(pthread_t));
    ts = (int*)calloc(CFG(context_thread), sizeof(int));
    for (i = 0; i < CFG(context_thread); i++) {
        ts[i] = i;
        pthread_create(&cthread[i], &attr, comsumer_thread, &ts[i]);
    }

    context_inited = 1;

    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        logmsg(stderr, "Unable to trap SIGUSR1, exiting...");
        exit(2);
    }

    logmsg(stdout, "Reading thread started.");

    /*logmsg(stdout, "Memory alloc detail:");*/
    /*malloc_stats();*/

    while (1)
   	{
        dp = opendir(CFG(read_dir));
        if (!dp) {
            logmsg(stderr, "Cannot open dir %s, please check. will retry in %d seconds",
                    CFG(read_dir), CFG(sleep_interval));
            goto retry;
        }

		// cp the filenames into a list
        num_of_file  = 0;
        max_num_file = 32;
        file_list = (file_info_t*)calloc(max_num_file, sizeof(file_info_t));
        while ((entry = readdir(dp)) != NULL)
	   	{
            char read_filename[256];
            sprintf(read_filename, "%s/%s", CFG(read_dir), entry->d_name);

            // skip useless file
            if ((strcmp(read_filename, ".") == 0)		||
                (strcmp(read_filename, "..") == 0)		||
                (lstat(read_filename, &file_stat) < 0)  ||
                (!S_ISREG(file_stat.st_mode))			||
				strstr(read_filename, ".tmp") != NULL)
			{
				 continue;
			}

            // doble the size of file_list when needed
            if (num_of_file == max_num_file)
		   	{
                file_info_t * tmp_list;
                max_num_file *= 2;
                tmp_list  = file_list;
                file_list = (file_info_t*)calloc(max_num_file, sizeof(file_info_t));
                memcpy(file_list, tmp_list, sizeof(file_info_t) * num_of_file);
                free(tmp_list);
            }

            // copy file info
            strcpy(file_list[num_of_file].filename, read_filename);
            num_of_file++;
        }

        // sort input file
        qsort(file_list, num_of_file, sizeof(file_info_t), file_list_comp);

        if (num_of_file) logmsg(stdout, "%d file will be processed this time.", num_of_file);

        for (i = 0; i < num_of_file; i++)
	   	{
            char file_line[MAX_LINE];
            char read_filename[256];
            FILE * rfile;
            signal_entry_t se;
            strcpy(read_filename, file_list[i].filename);
            logmsg(stdout, "Processing file %s", read_filename);
            rfile = fopen(read_filename, "r");
            while (read_line(rfile, file_line, 1024) != NULL)
		   	{
                if (parse_line(file_line, &se))
				   	continue;
                push_to_sort_buf(&se);
            }

            // remove read file
            fclose(rfile);
            remove(read_filename);

            // check exit flag
            if (check_exit_flag())
		   	{
                exit_flag = 1;
                break;
            }

            if (need_update_area_cell_map)
		   	{
                int ret = 0;
                need_update_area_cell_map = 0;
                logmsg(stdout, "Updateing area_cell_map from %s on SIGUSR1",
                                CFG(area_cell_map));
                ret = read_cell_map(CFG(area_cell_map));
                logmsg(stdout, "%d map loaded", ret);
            }
        }

        closedir(dp);

        if (check_exit_flag()) exit_flag = 1;

        if (exit_flag)
	   	{
            logmsg(stdout, "Exitflag recived, exiting read thread...");
            // exiting
            exit_flag = 1;
            // wake all consumer thread
            for (i = 0; i < CFG(context_thread); i++) {
                pthread_cond_signal(&context_thread[i].pushed);
            }
            for (i = 0; i < CFG(context_thread); i++) {
                pthread_join(cthread[i], NULL);
            }
            write_back_context();
            dump_context(backup_filename);
            remove(g_exitflag_file);
            break;
        }

        // free allocted resource
        free(file_list);
retry:
		logdbg(stdout, "Now sleep[%d] seconds", CFG(sleep_interval));
        sleep(CFG(sleep_interval));
    }
    return NULL;
}

static int push_to_sort_buf(signal_entry_t * se)
{
    time_t min = se->timestamp / 60;
    int slot = min % CFG(sort_min);
    int i  = 0;
    int ps = 0;
    int ret = 0;

	char time_str[20], time_now[20];
	memset(time_str, 0, sizeof(time_str));
	memset(time_now, 0, sizeof(time_now));
	timestamp_str( signal_current*60, time_str );
	timestamp_str( se->timestamp, time_now );

    // discard timeout record
    if (min <= signal_current - CFG(sort_min))
   	{
        /*logdbg(stderr, "Discard record due to latency. imsi[%s] signal_current[%s] time_now[%s] latency[%d]", se->imsi, time_str, time_now,  min-signal_current);*/
        return 0;
    }

    if (context_sort_buf[slot].time < min)
   	{
        signal_current = min;

        // need push sort buffer to context thread
        for (i = 0; i < CFG(sort_min) ; i++)
	   	{
            ps = (i + slot + 1) % CFG(sort_min);
            if (context_sort_buf[ps].time > context_sort_buf[slot].time)
				continue;
            if (context_sort_buf[ps].time == -1)
			   	continue;

            // we need to push this sort buffer into context
			char pushed_time[20], now_time[20];
			timestamp_str( context_sort_buf[ps].time * 60, pushed_time );
			timestamp_str( signal_current * 60, now_time );
            logdbg(stdout, "Push context_sort_buf[%d]'s [%d] records to context, now_time[%s] pushed_time[%s]", ps, context_sort_buf[ps].used, now_time, pushed_time);

            // check hourly update when needed
            check_hourly_update(context_sort_buf[ps].time * 60);

            // daily cleanup?
            if (((context_sort_buf[ps].time * 60 + CFG(tz_offset)) / 60 - CFG(cleanup_min)) / 60 % 24 == CFG(cleanup_hour))
		   	{
                if (need_daily_cleanup == 1)
			   	{
                    logmsg(stdout, "cleanup started");
                    ret = daily_cleanup(context_sort_buf[ps].time * 60);
                    logmsg(stdout, "Updateing area_cell_map from %s", CFG(area_cell_map));
                    ret = read_cell_map(CFG(area_cell_map));
                    logmsg(stdout, "%d map loaded", ret);
                }
                need_daily_cleanup = 0;
            } else {
                need_daily_cleanup = 1;
            }

            // do the sort.
            qsort(context_sort_buf[ps].buffer, context_sort_buf[ps].used, sizeof(context_content_t), comp_signal_func);

            // push to context
            push_to_context(&context_sort_buf[ps]);

            // reset sort buffer
            context_sort_buf[ps].used = 0;
            context_sort_buf[ps].time = -1;
        }
    }

    if (context_sort_buf[slot].time == -1)
   	{
        // new minute
        context_sort_buf[slot].time = min;
        context_sort_buf[slot].used = 0;
    }

    if (context_sort_buf[slot].used == CFG(sort_buffer))
   	{
        logmsg(stderr, "Sort Buffer Full, please check.");
        return 0;
    }

    memcpy(&context_sort_buf[slot].buffer[context_sort_buf[slot].used], se, sizeof(context_content_t));

    context_sort_buf[slot].used += 1;

	logdbg( stdout, "push[%s] to context_sort_buf[%d] buffer[%d]\n", se->imsi, slot, context_sort_buf[slot].used -1 );
    return 0;
}

static int push_to_context(context_sort_buffer_t * csb)
{
	// pushed 标示context_sort_buf中buffer已push的位置
    int cidx = 0, pushed = 0, to_push = 0;
    int num = csb->used;
    signal_entry_t * ses = csb->buffer;
    context_thread_t * ct;

    while (pushed < num)
   	{
        // get num of context to push
        ct = &context_thread[cidx];

#define _DO_PUSH(st, pnum) do { \
    to_push = MIN(num - pushed, pnum); \
    memcpy(&ct->buf[st], &ses[pushed], sizeof(signal_entry_t) * to_push); \
    pushed   += to_push; \
    ct->used += to_push; \
	logdbg(stdout, "context_thread[%d] buf_index[%d] total[%d] records pushed. read[%d] used[%d]", cidx, st-to_push, to_push, ct->read, ct->used); \
} while(0)

        pthread_mutex_lock(&ct->mutex);

        if (ct->read + ct->used < CONTEXT_BUF_CACHED)
		{
			/*logdbg(stdout, "context_thread[%d] read[%d] used[%d] ", cidx, ct->read, ct->used);*/
            _DO_PUSH(ct->read + ct->used, CONTEXT_BUF_CACHED - ct->read - ct->used);
            /*_DO_PUSH(0, ct->read);*/
        } else {
            _DO_PUSH(ct->read + ct->used - CONTEXT_BUF_CACHED, CONTEXT_BUF_CACHED - ct->used);
        }
#undef _DO_PUSH

        pthread_mutex_unlock(&ct->mutex);

        pthread_cond_signal(&ct->pushed);
        cidx = (cidx + 1) % CFG(context_thread);
    }

    return 0;
}

static char * read_line(FILE * f, char * line, size_t byte)
{
    char * ret = fgets(line, byte, f);
    int  i = 0;
    if (!ret) return NULL;

    // trim EOL
    for (i = strlen(ret) - 1; i >= 0; i--) {
        if (ret[i] == '\r' || ret[i] == '\n') ret[i] = 0;
    }
    return ret;
}

static int check_exit_flag()
{
    FILE * exit_f;
    exit_f = fopen(g_exitflag_file, "r");
    if (exit_f) fclose(exit_f);
    return exit_f? 1: 0;
}

static int init_sort_buffer()
{
    int flag = 0;
    int size = 0;
    int i = 0;

    logmsg(stdout, "Starting to allocate sort buffer");
    size = CFG(sort_min) * sizeof(context_sort_buffer_t);
    context_sort_buf = (context_sort_buffer_t *)malloc(size);
    size = CFG(sort_buffer) * sizeof(context_content_t);
    flag = 0;
    for (i = 0; i < CFG(sort_min); i++)
   	{
        context_sort_buf[i].size   = CFG(sort_buffer);
        context_sort_buf[i].time   = -1;
        context_sort_buf[i].used   = 0;
        context_sort_buf[i].buffer = (signal_entry_t*)malloc(size);
        if (!context_sort_buf[i].buffer) {
            flag = 1;
            break;
        }
        memset(context_sort_buf[i].buffer, 0, size);
    }

    if (flag) {
        logmsg(stderr, "Cannot alloc memory for Sort buffer, exiting...");
        exit(2);
    }

    logmsg(stdout, "\t%.3fMB x %d allocated for sort buffer", size / 1024.0 / 1024, CFG(sort_min));
    return 0;
}

// write buffer back to
static int write_back_context()
{
    int i = 0, j = 1, l = 0;
    signal_entry_t * se = NULL;
    FILE * wfile = NULL;
    char line[256];
    char filename[256];

    for (i = 0; i < CFG(sort_min); i++)
   	{
        if (context_sort_buf[i].time == -1) continue;
        // filename
        snprintf(filename, 255, "%s/%s_%d",
            CFG(read_dir), "00_wb", (int)context_sort_buf[i].time);
        logmsg(stdout, "Write back contect to file %s", filename);
        wfile = fopen(filename, "w");
        for (j = 0; j < context_sort_buf[i].used; j++)
	   	{
            se = &context_sort_buf[i].buffer[j];
            l = write_back_format(se, line);
            fwrite(line, l, 1, wfile);
        }
        fflush(wfile);
        fclose(wfile);
    }
    return 0;
}

static int write_back_format(const signal_entry_t * c, char * line)
{
    char tmp_time[27];
    int ret = 0;
    char lac_cell[12];
    char * cell_off = lac_cell;

    memset(tmp_time, 0, 27);
    time_format(c->timestamp, tmp_time);

    strncpy(lac_cell, c->lac_cell, 12);
    cell_off = strchr(lac_cell, '-');
    *(cell_off++) = '\0';

    ret = sprintf(line, "%s,%s,%3hu,%s,%s\n",
            c->imsi, tmp_time, c->event, lac_cell, cell_off);

    return ret;
}

static int time_format(const time_t t, char * line)
{
    struct tm tp;
    if (t > 0) {
        localtime_r(&t, &tp);
        strftime(line, 27, "%Y-%m-%d %H:%M:%S", &tp);
    } else {
        line[0] = '\0';
    }
    return 0;
}

static void sigusr1_handler(int signo)
{
    signal(SIGUSR1, sigusr1_handler);
    logmsg(stdout, "SIGUSR1 caught, area_cell_map will reload on next file");
    need_update_area_cell_map = 1;
    return;
}
