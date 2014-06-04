/*
 * file: sscontext.c
 * holds method to dealing with context
 * all function here *MUST* be thread safe
 * but not necessery be reentrant
 *
 */

#ifndef _THREAD_SAFE_
#error "Must be compiled with -D_THREAD_SAFE_"
#endif

// we need strptime
#ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE
#endif

#define ROUND(a, b) ((a) / (b) * (b))

#ifndef MAX
#  define MAX(a, b) ((a) > (b)? (a): (b))
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

#include <time.h>
#include <pthread.h>

#include "config.h"
#include "context.h"
#include "reader-thread.h"
#include "comsumer-thread.h"
#include "log.h"

typedef struct output_file_t_tag
{
    char filename[256];
    time_t filename_ts;
    FILE * output;
} output_file_t;

typedef struct area_cell_map_t_tag
{
    char area_id[12];
    char lac_cell[LEN_LACCELL];
} area_cell_map_t;

area_cell_map_t * area_cell_map = NULL;
uint32_t num_of_area_cell_map = 0;

// init output file;
static output_file_t output_info = {"", -1, NULL};

int context_inited = 0;

static inline uint64_t BKDRHash(const char * str);
static int time_format(const time_t t, char * line);
static int do_write_file(const context_content_t * c, const area_t * a);
static int do_update_context(context_content_t * old, const signal_entry_t * new);
static int do_new_context(context_content_t * cc, const signal_entry_t * se);
static int format_context(const context_content_t * c,
                          const area_t * a, char * line);
static int hourly_update_context(time_t time);
static int do_update_event_stat(area_t * cc, const signal_entry_t * se);
static int restore_one_context(context_content_t * c);
static int write_context(const context_content_t * content, const area_t * a);
static int find_area_id(const char * lac_cell, area_cell_map_t ** start);

static inline uint64_t BKDRHash(const char * str)
{
    static const unsigned int seed = 1313; // 31 131 1313 13131 131313 etc..
    uint64_t hash0 = 0;
    while (*str) hash0 = hash0 * seed + (*str++);
    return hash0;
}

// update context. invoke do_update when content is already in context
// otherwise insert new content into context.
int update_context(signal_entry_t * se)
{
    uint64_t hash_in = BKDRHash(se->imsi) % context.size;
    uint64_t part    = hash_in % context.part;
    uint64_t offset  = hash_in / context.part;
    int ret = 0;
    int flag_content_found = 0;

    context_seg_t * context_seg = &context.contexts[part];
    context_content_t * temp_content;

    // lookup content in content_seg
    flag_content_found = 0;

    // lock {{
    pthread_mutex_lock(&context_seg->mutex_lock);
    for (temp_content = &context_seg->content[offset];
         temp_content && temp_content->imsi[0];
         temp_content = temp_content->next)
   	{
        if (0 == strcmp(se->imsi, temp_content->imsi))
	   	{
            flag_content_found = 1;
            break;
        }
    }

    // invoke new content or insert new content
    if (flag_content_found) {
        ret = do_update_context(temp_content, se);
    } else {
        ret = do_new_context(&context_seg->content[offset], se);
    }
    pthread_mutex_unlock(&context_seg->mutex_lock);
    // unlock }}

    if (ret) {
        logmsg(stderr, "Update Context failed");
        return ret;
    }

    return 0;
}

#define APPEND_NEW_AREA(x, se, a) do { \
    area_t * new_area_tmp = (area_t*)calloc(1, sizeof(area_t)); \
    strcpy(new_area_tmp->area_id, (a)); \
    new_area_tmp->come_time       = (se)->timestamp; \
    new_area_tmp->last_event_time = (se)->timestamp; \
    new_area_tmp->next = (x); \
    do_update_event_stat(new_area_tmp, se); \
    (x) = new_area_tmp; \
	logmsg(stdout, "Insert a new context_content_t area_id[%s] come_time[%d] last_event_time[%d] last_event_type[%d]", new_area_tmp->area_id);\
} while (0)

static int do_update_context(context_content_t * cc, const signal_entry_t * se)
{
    int flag_area_found = 0, i = 0;

    // lookup area_cell_map
    area_cell_map_t * map = NULL;
    int num_of_area = find_area_id(se->lac_cell, &map);

    area_t ** area = NULL;

    // update old areas, leaveing and staying
    for (area = &cc->areas; *area; )
   	{
        flag_area_found = 0;
        for (i = 0; i < num_of_area; i++)
	   	{
            if (0 == strcmp(map[i].area_id, (*area)->area_id)) {
                flag_area_found = 1;
                break;
            }
        }

        if (flag_area_found) {
            (*area)->last_event_time = se->timestamp;
            do_update_event_stat(*area, se);
            area = &(*area)->next;
        } else {
            // user leave area, output context
            (*area)->leave_time = se->timestamp;
            time_t pstart = ROUND((*area)->leave_time - 1, CFG(output_interval));
            (*area)->resident_time = (*area)->leave_time - MAX((*area)->come_time, pstart);
            write_context(cc, (*area));
            // then delete
            area_t * tmp_area = *area;
            *area = (*area)->next;
            cc->num_of_area --;
            if (tmp_area) free(tmp_area);
        }
    }

    // user enter new area
    area_t ** tail_next = area;
    area_t *  new_areas = NULL;

    for (i = 0; i < num_of_area; i++)
   	{
        flag_area_found = 0;
        for (area = &cc->areas; *area; area = &(*area)->next)
	   	{
            if (0 == strcmp(map[i].area_id, (*area)->area_id)) {
                flag_area_found = 1;
                break;
            }
        }
        if (!flag_area_found) {
            APPEND_NEW_AREA(new_areas, se, map[i].area_id);
            cc->num_of_area++;
        }
    }

    *tail_next = new_areas;

    return 0;
}

static int do_new_context(context_content_t * cc, const signal_entry_t * se)
{
    context_content_t * tmp_cc = NULL;
    int i = 0;

    // lookup area_cell_map
    area_cell_map_t * map = NULL;
    int num_of_area = find_area_id(se->lac_cell, &map);
    if (num_of_area == 0) return 0;

    if (!cc->imsi[0]) {
        // newcontext node in this hash slot
        tmp_cc = cc;
        cc->next = NULL;
    } else {
        // prefix a new node
        tmp_cc = (context_content_t*)calloc(1, sizeof(context_content_t));
        tmp_cc->next = cc;
        cc->next = tmp_cc;
    }
    strcpy(tmp_cc->imsi, se->imsi);

    // user enter new area
    tmp_cc->num_of_area = num_of_area;
    for (i = 0; i < num_of_area; i++)
   	{
        APPEND_NEW_AREA(cc->areas, se, map[i].area_id);
    }
    return 0;
}

static int do_update_event_stat(area_t * cc, const signal_entry_t * se)
{
    switch (se->event)
   	{
        case 101:
            cc->calling_call_counts++;
            break;
        case 102:
            cc->called_call_counts++;
            break;
        case 201:
            cc->smo_sms_counts++;
            break;
        case 202:
            cc->smt_sms_counts++;
            break;
        case 501:
            cc->mobile_open_counts++;
            cc->last_open_time = se->timestamp;
            break;
        case 502:
            cc->mobile_close_counts++;
            cc->last_close_time = se->timestamp;
            break;
        default: break;
    }
    return 0;
}

static int do_write_file(const context_content_t * cc, const area_t * a)
{
    char line[LEN_LINE];
    int  len = 0;
    memset(line, 0, LEN_LINE);

    // format Context to line
    len = format_context(cc, a, line);
    if (!output_info.output) output_info.output = fopen(CFG(tmp_filename), "a");
    // according to POSIX standard, fwrite is atomic, therefore no lock is needed.
	printf("out line: %s", line);
    fwrite(line, len, 1, output_info.output);
    return 0;
}

static int write_context(const context_content_t * content, const area_t * area)
{
    return do_write_file(content, area);
}

static int format_context(const context_content_t * c, const area_t * a, char * line)
{
    int ret = 0;
    char time_laop[27], time_lacl[27], time_come[37], time_leav[27];

    time_format(a->last_open_time,  time_laop);
    time_format(a->last_close_time, time_lacl);
    time_format(a->come_time,       time_come);
    time_format(a->leave_time,      time_leav);

    ret = sprintf(line,
     /*  1  2  3  4  5  6  7  8  9  10 11 12 13 */
        "%s,%s,%s,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d\n",
        c->imsi, a->area_id,                       // 1, 2
        time_laop, time_lacl, time_come, time_leav, // 3, 4, 5, 6
        a->resident_time,                           // 7
        a->mobile_open_counts, a->mobile_close_counts, // 8, 9
        a->smo_sms_counts,     a->calling_call_counts, // 10, 11
        a->smt_sms_counts,     a->called_call_counts); // 12, 13

    return ret;
}

int fmt_output_timestamp(const time_t t, char * line)
{
    struct tm tp;
    if (t != 0) {
        localtime_r(&t, &tp);
        strftime(line, 27, "%Y%m%d%H%M", &tp);
    }
    return 0;
}

int time_format(const time_t t, char * line)
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

int hourly_update_context(time_t time)
{
    int i = 0, j = 0, k = 0;
    context_seg_t     * ch = NULL;
    context_content_t * cc = NULL;
    char output_timestamp[24];
    int total_in_context = 0;
    int total_area       = 0;

    // wait all context thread finished.
    wait_context_thread();

    // lock all context
    for (i = 0; i < context.part; i++)
   	{
        pthread_mutex_lock(&context.contexts[i].mutex_lock);
    }

    for (i = 0; i < context.part; i++)
   	{
        ch = &context.contexts[i];
        for (j = 0; j < ch->size; j++)
	   	{
            for (cc = &ch->content[j]; cc && cc->imsi[0]; cc = cc -> next)
		   	{
                area_t * area = NULL;
                total_in_context++;
                for (k = 0, area = cc->areas;
                     k < cc->num_of_area;
                     k++, area = area->next)
			   	{
                    total_area ++;
                    time_t ps = MAX(time - CFG(output_interval), area->come_time);
                    area->resident_time = time - ps;
                    write_context(cc, area);
                    area->mobile_open_counts  = 0;
                    area->mobile_close_counts = 0;
                    area->calling_call_counts = 0;
                    area->called_call_counts  = 0;
                    area->smo_sms_counts      = 0;
                    area->smt_sms_counts      = 0;
                }
            }
        }
    }

    // switch to new file
    if (output_info.output) {
        fflush(output_info.output);
        fclose(output_info.output);
    }
    fmt_output_timestamp(output_info.filename_ts / CFG(output_interval) * CFG(output_interval), output_timestamp);
    // trim min if CFG(output_interval) is multiply of 3600
    if (CFG(output_interval) % 3600 == 0) output_timestamp[10] = '\0';
    // trim hour if daily output
    if (CFG(output_interval) % (24 * 3600) == 0) output_timestamp[8] = '\0';
    sprintf(output_info.filename, "%s/%s%s%s",
                CFG(output_dir),
                CFG(output_prefix),
                output_timestamp,
                CFG(output_suffix));

    logmsg(stdout, "Output File: %s, Context: %d, area: %d",
           output_info.filename, total_in_context, total_area);

    if (CFG(cross_mountpoint)) {
        // using mv to move file, rename() cannot handle
        // moving a file cross mount points
        char cmd_buffer[1024];
        sprintf(cmd_buffer, "mv \'%s\' \'%s\'", CFG(tmp_filename), output_info.filename);
        logmsg(stdout, "Ecec shell cmd: %s", cmd_buffer);
        system(cmd_buffer);
    } else {
        logmsg(stdout, "Moving %s to %s", CFG(tmp_filename), output_info.filename);
        rename(CFG(tmp_filename), output_info.filename);
    }

    output_info.output = fopen(CFG(tmp_filename), "a");
    // unlock all context
    for (i = 0; i < context.part; i++) {
        pthread_mutex_unlock(&context.contexts[i].mutex_lock);
    }
    output_info.filename_ts = time;
    return 0;
}

int parse_line(const char * line, signal_entry_t * se)
{
    int pos = 0;
    char  word[15][1024];
	int i=0;
	for( ; i<15; i++ ) {
		memset(word[i], 0, 1024);
	}

    int  word_index = 0, letter_index = 0;
	/*struct tm tmp_tm;*/
	time_t tmp_tm;

    while (line[pos])
   	{
        if (line[pos] == ',')
	   	{
            word[word_index][letter_index] = '\0';
			word_index++;
			letter_index=0;
		}else{
			word[word_index][letter_index] = line[pos];
			letter_index++;
		}
		pos++;
    }

    // ckeck num of field
    if (word_index != NUM_OF_FIELD && word_index != NUM_OF_FIELD + 1) {
        logmsg(stderr, "Wrong num of field, need %d, got %d, line: %s", NUM_OF_FIELD, word_index, line);
        return 1;
    }

    strcpy(se->imsi, word[decode(imsi)]);
    sprintf(se->lac_cell, "%s-%s", word[decode(lac)], word[decode(cell)]);
	/*strptime(word[decode(timestamp)], "%Y-%m-%d %H:%M:%S", &tmp_tm);*/
	/*se->timestamp = mktime(&tmp_tm);*/
	str_timestamp(word[decode(timestamp)], &tmp_tm);
	se->timestamp = tmp_tm;
    se->event = atoi(word[decode(event_type)]);
    return 0;
}

int check_hourly_update(time_t t)
{
    time_t rd_t = (t + CFG(tz_offset)) / CFG(output_interval) * CFG(output_interval) - CFG(tz_offset);

    // time
    if (output_info.filename_ts == -1) output_info.filename_ts = rd_t;
#ifdef __DEBUG__
    printf("%d, %d, %d\n", t, rd_t, output_info.filename_ts);
#endif
    // need hourly update
    if (rd_t != output_info.filename_ts) {
        logmsg(stdout, "Hourly update: %d -> %d", rd_t, output_info.filename_ts);
        // need switch file name
        hourly_update_context(rd_t);
    }
    return 0;
}

int daily_cleanup(time_t t)
{
    static const int sec_in_day = 24 * 3600;
    int i = 0, j = 0;
    int cleaned_user = 0, cleaned_area = 0;

    context_content_t * cc = NULL;
    context_seg_t     * ch = NULL;
    context_content_t * c_next = NULL;
    context_content_t * c_prev = NULL;

    // wait context thread
    wait_context_thread();

    for (i = 0; i < context.part; i++)
   	{
        ch = &context.contexts[i];
        for (j = 0; j < ch->size; j++)
	   	{
            cc = &ch->content[j];
            c_prev = NULL;
            while (cc && cc->imsi[0])
		   	{
                area_t ** parea = NULL;
                for (parea = &cc->areas; *parea; )
			   	{
                    if (t - (*parea)->last_event_time > sec_in_day) {
                        // area timeout!!
                        cc->num_of_area --;
                        cleaned_area ++;
                        (*parea)->resident_time = (1 - CFG(cleanup_mark))
                            * (t - MAX(t - CFG(output_interval), (*parea)->come_time));
                        if (CFG(cleanup_mark)) write_context(cc, *parea);
                        area_t * to_be_free = *parea;
                        parea = &(*parea)->next;
                        if (to_be_free) free(to_be_free);
                    } else {
                        parea = &(*parea)->next;
                    }
                }

                // if user still in at least 1 area, leave it be
                if (cc->num_of_area) continue;

                cleaned_user++;
                if (c_prev == NULL) {

                    // remove first node
                    if (!c_next) {
                        // next is null
                        memset(cc, 0, sizeof(context_content_t));
                    } else {
                        memcpy(cc, c_next, sizeof(context_content_t));
                        free(c_next);
                    }
                } else {
                    c_prev->next = cc->next;
                    free(cc);
                    cc = c_prev->next;
                }
            }
        }
    }

    logmsg(stdout, "%d user, %d area cleaned", cleaned_user, cleaned_area);
    logmsg(stdout, "Memory alloc detail:");
    malloc_stats();

    return 0;
}

// dump all context to file
int dump_context(const char * filename)
{
    FILE * dump_file = fopen(filename, "wb");
    int i = 0, j = 0;

    context_content_t * cc = NULL;

    if (!dump_file) {
        logmsg(stderr, "Open backup file %s fail, context not backup", filename);
        return 1;
    }

    logmsg(stdout, "Backing up context to %s", filename);
    // write all context
    for (i = 0; i < CONTEXT_PART; i++)
   	{
        for (j = 0; j < context.contexts[i].size; j++)
	   	{
            for (cc = &context.contexts[i].content[j]; cc && cc->imsi[0]; cc = cc->next)
		   	{
                if (cc->num_of_area == 0) continue;
                fwrite(cc, sizeof(context_content_t), 1, dump_file);
                area_t * area = NULL;
                for (area = cc->areas; area; area = area->next){
                    fwrite(area, sizeof(area_t), 1, dump_file);

				}
			}
		}
	}

    fflush(dump_file);
    fclose(dump_file);
    logmsg(stdout, "Backing up context done!", filename);
    return 0;
}

// restore context from backup file
int restore_context(const char * filename)
{
    FILE * dump_file = fopen(filename, "rb");
    context_content_t * cc;
    int backuped_user = 0, backuped_area = 0, i = 0;

    if (!dump_file) {
        logmsg(stderr, "Backup file %s not found, ignored", filename);
        return 1;
    }

    logmsg(stdout, "Restoring context from %s", filename);
    cc = (context_content_t *)malloc(sizeof(context_content_t) * 1);
    while (fread(cc, sizeof(context_content_t), 1, dump_file))
   	{
        cc->next = NULL;
        area_t * area = NULL;
        for (i = 0; i < cc->num_of_area; i++) {
            fread(area, sizeof(area_t), 1, dump_file);
            cc->areas->next = area;
            area->next      = cc->areas;
            backuped_area ++;
        }
        restore_one_context(cc);
        backuped_user++;
    }
    fclose(dump_file);
    logmsg(stdout, "%d Context, %d area retored",
           backuped_user, backuped_area, filename);
    free(cc);
    remove(filename);
    return 0;
}

static int restore_one_context(context_content_t * c)
{
    uint64_t hash_in = BKDRHash(c->imsi) % context.size;
    uint64_t part    = hash_in % context.part;
    uint64_t offset  = hash_in / context.part;

    context_seg_t * context_seg = &context.contexts[part];
    context_content_t * curr = &context_seg->content[offset];

    if (curr->imsi[0]) {
        // new node
        context_content_t * new = (context_content_t*)
            calloc(1, sizeof(context_content_t));
        memcpy(curr, c, sizeof(context_content_t));
        new->next  = curr->next;
        curr->next = new;
    } else {
        memcpy(curr, c, sizeof(context_content_t));
        curr->next = NULL;
    }

    return 0;
}

int init_context()
{
    int i = 0;
    int size_per_part = 0;

    memset(&context, 0, sizeof(context_t));

    context.size = CFG(context_size);
    context.part = CONTEXT_PART;
    size_per_part = context.size / context.part;

    logmsg(stdout, "Starting to allocated Context: %dbyte x %dM x %d = %1.3fMB",
            sizeof(context_content_t),
			size_per_part / 1000000,
			context.part,
            10 * size_per_part * sizeof(context_content_t) / 1024.0 / 1024);

    for (i = 0; i < context.part; i++)
   	{
        context.contexts[i].size = size_per_part;
        pthread_mutex_init(&context.contexts[i].mutex_lock, NULL);
        context.contexts[i].content = calloc(size_per_part, sizeof(context_content_t));
        if (!context.contexts[i].content) return CONTEXT_ERR_MALLOC;
    }
    logmsg(stdout, "Context allocated");

    // read area_cell_map
    logmsg(stdout, "Init area_cell_map with %s", CFG(area_cell_map));
    i = read_cell_map(CFG(area_cell_map));
    if (i == 0) {
        logmsg(stderr, "Nothing readed, exiting...");
        exit(1);
    }
    logmsg(stdout, "%d area_cell_map loaded", i);

    return 0;
}

int read_cell_map(const char * filename)
{
    FILE * map_file = NULL;
    uint32_t new_num = 0;
    uint32_t i = 0;
    char line[1024];

    do {
        map_file = fopen(filename, "r");

		if (!map_file){
			break;
		}

        // free old area_cell_map
        if (area_cell_map){
			free(area_cell_map);
		}

        // get num of area_cell_map
		while (EOF != fscanf(map_file, "%s\n", line)){
			new_num++;
		}
        if (new_num == 0) break;
        fseek(map_file, 0, SEEK_SET);

        // free old area_cell_map
        if (area_cell_map) free(area_cell_map);
        num_of_area_cell_map = 0;

        // alloc new area_cell_map
        area_cell_map = (area_cell_map_t*)calloc(new_num, sizeof(area_cell_map_t));
        if (!area_cell_map) break;

        // read new area_cell_map
        for (i = 0; i < new_num; i++)
	   	{
            fscanf(map_file, "%s\n", line);
            char * pos_comma = strchr(line, ',');
            pos_comma[0] = 0;
            strcpy(area_cell_map[i].lac_cell, line);
            strcpy(area_cell_map[i].area_id, pos_comma + 1);
        }

        num_of_area_cell_map = new_num;
        return new_num;
    } while(0);

    // error
    if (num_of_area_cell_map == 0) {
        logmsg(stderr, "Cannot init are_cell_map, exiting...");
        return 0;
    } else {
        logmsg(stdout, "Reloading area_cell_map fail, ignored.");
        return num_of_area_cell_map;
    }
}

static int find_area_id(const char * lac_cell, area_cell_map_t ** start)
{
    int l = 0, r = num_of_area_cell_map, m = 0;
    int res = 0, found = 0;

    while (l < r)
   	{
        m = (l + r) / 2;
        res = strcmp(lac_cell, area_cell_map[m].lac_cell);

        if (res > 0) {
            l = m + 1;
        } else if (res < 0) {
            r = m;
        } else {
            found = 1;
            break;
        }
    }

    if (found)
   	{
        l = r = m;
        // trace back
        while (l >= 0 && 0 == strcmp(lac_cell, area_cell_map[l - 1].lac_cell))
            l--;
        // trace forward
        while (r < num_of_area_cell_map &&
               0 == strcmp(lac_cell, area_cell_map[r + 1].lac_cell)) r++;
        *start = &area_cell_map[l];
        return r - l + 1;
    } else {
        *start = NULL;
        return 0;
    }
}

void str_timestamp( const char * str, time_t * tm )
{
	struct tm tmp_tm;
	strptime(str, "%Y-%m-%d %H:%M:%S", &tmp_tm);
	*tm = mktime(&tmp_tm);
}

void timestamp_str( const time_t t, char * str )
{
	struct tm * timeinfo;

	timeinfo = localtime(&t);
	strftime(str, 80, "%Y-%m-%d %H:%M:%S", timeinfo);
}
