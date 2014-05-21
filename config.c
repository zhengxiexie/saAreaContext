#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "log.h"

config_t g_config;

const int output_interval_val[] = { 3600, 1800, 3600, 7200, 86400};

#define SIZE_OF(x) (sizeof(x) / sizeof(x[0]))

char * get_line(FILE * f, char * line, int len);

def_decode(imsi);
def_decode(timestamp);
def_decode(event_type);
def_decode(lac);
def_decode(cell);

int read_config(const char * filename)
{
    FILE * cfg_file;
    char word[2][256];
    char line[1024];
    int flag_indq = 1;
    int idx_w     = 0;
    int pos_w     = 0;
    int pos       = 0;
    cfg_file = fopen(filename, "r");
    if (!cfg_file) return -1;

    while (get_line(cfg_file, line, 1024) != NULL) {
        word[0][0] = '\0';
        word[1][0] = '\0';

        // get word[0] and word[1]
        if (line[0] == '\0') continue;
        pos   = 0;
        idx_w = 0;
        pos_w = 0;
        flag_indq = 0;
        memset(word[0], 0, 256);
        memset(word[1], 0, 256);
        while ((idx_w < 2) && (line[pos])) {
            if (line[pos] == '\"') {
                flag_indq = 1 - flag_indq;
                pos++;
            }
            if (line[pos] == '\t' || line[pos] == ' ') {
                if (flag_indq) {
                    word[idx_w][pos_w++] = line[pos];
                    pos++;
                } else {
                    word[idx_w++][pos_w + 1] = 0;
                    pos_w = 0;
                    while (line[pos] == '\t' || line[pos] == ' ') if (line[pos]) pos++;
                }
            } else {
                word[idx_w][pos_w++] = line[pos];
                pos++;
            }
        }

#define _GET_STR_CFG(x) if (0 == strcasecmp(word[0], #x)) { \
    strcpy(g_config._##x, word[1]); \
    logmsg(stdout, "\tconfig: [%s] = [%s]", #x, word[1]); \
    continue; \
}
#define _GET_INT_CFG(x) if (0 == strcasecmp(word[0], #x)) { \
    g_config._##x = atoi(word[1]); \
    logmsg(stdout, "\tconfig: [%s] = [%d]", #x, g_config._##x); \
    continue; \
}
        _GET_STR_CFG(output_dir);
        _GET_STR_CFG(output_prefix);
        _GET_STR_CFG(output_suffix);
        _GET_STR_CFG(read_dir);
        _GET_STR_CFG(tmp_filename);
        _GET_STR_CFG(area_cell_map);
        _GET_STR_CFG(backup_dir);
        _GET_INT_CFG(sort_min);
        _GET_INT_CFG(sort_buffer);
        _GET_INT_CFG(sleep_interval);
        _GET_INT_CFG(output_interval);
        _GET_INT_CFG(context_size);
        _GET_INT_CFG(context_thread);
        _GET_INT_CFG(cleanup_mark);
        _GET_INT_CFG(tz_offset);
        _GET_INT_CFG(cleanup_min);
        _GET_INT_CFG(cleanup_hour);
        _GET_INT_CFG(cross_mountpoint);
#undef _GET_STR_CFG
#undef _GET_INT_CFG
    }
    fclose(cfg_file);

    // post processing config
    g_config._context_size *= CONTEXT_BASESIZE;
    g_config._sort_buffer  *= CONTEXT_SORT_BASESIZE;
    g_config._output_interval = (g_config._output_interval > SIZE_OF(output_interval_val)
                                || g_config._output_interval < 0)?
                                3600:
                                output_interval_val[g_config._output_interval];
    logmsg(stdout, "output interval = %d", g_config._output_interval);
    return 0;
}

int read_decode_map(const char * filename)
{
    FILE * f;
    char line[256];
    char * comma;

    f = fopen(filename, "r");
    if (!f) return 1;

    while (get_line(f, line, 256) != NULL) {
        if (line[0] == 0) continue;
        comma = strchr(line, ',');
        if (!comma) continue;
        *comma = 0;
        comma ++;

#define _GET_DECODE(x) if (0 == strcasecmp(line, #x)) { \
    decode(x) = atoi(comma); \
    logmsg(stdout, "\tdecode(%s) = %d", #x, decode(x)); \
    continue; \
}
        _GET_DECODE(imsi);
        _GET_DECODE(timestamp);
        _GET_DECODE(event_type);
        _GET_DECODE(lac);
        _GET_DECODE(cell);
    }
#undef _GET_DECODE

    fclose(f);
    return 0;
}

char * get_line(FILE * f, char * line, int len)
{
    char * ret = fgets(line, len, f);
    int i = 0;
    if (!ret) return NULL;

    // comment?
    if (ret[0] == '#') ret[0] = 0;

    // trim any EOL
    for (i = strlen(ret) - 1; i >= 0; i--) {
        if (ret[i] == '\r' || ret[i] == '\n') ret[i] = 0;
    }
    return ret;
}
