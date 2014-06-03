#ifndef __CONFIG_H__
#define __CONFIG_H__

#define def_decode(x) int field_##x = -1
#define export_decode(x) extern int field_##x
#define decode(x) field_##x

#define LEN_LACCELL (12)
#define LEN_LINE (1024)
#define CONTEXT_BASESIZE      (1000000ul)
#define CONTEXT_SORT_BASESIZE (100000ul)
#define CONTEXT_PART (10)
#define CFG(x) (g_config._##x)
#define NUM_OF_FIELD (7)

export_decode(imsi);
export_decode(timestamp);
export_decode(event_type);
export_decode(lac);
export_decode(cell);

typedef struct _config_t {
    int  _sort_min;
    int  _sort_buffer;
    int  _output_interval;
    int  _context_size;
    int  _context_thread;
    int  _sleep_interval;
    int  _cleanup_mark;
    int  _cleanup_min;
    int  _cleanup_hour;
    int  _cross_mountpoint;
    time_t _tz_offset;
    char _output_dir[128];
    char _output_prefix[64];
    char _output_suffix[64];
    char _read_dir[128];
    char _tmp_filename[256];
    char _area_cell_map[256];
    char _backup_dir[128];
}config_t;

extern config_t g_config;

int read_config(const char * cfg_file);
int read_decode_map(const char * file);

#endif /* __CONFIG_H__ */

