#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "config.h"
#include "comsumer-thread.h"
#include "reader-thread.h"
#include "context.h"
#include "log.h"

context_t context;
context_sort_buffer_t * context_sort_buf = NULL;
pthread_attr_t attr;

int main(int argc, char * argv[])
{
	print_logo("apple");

    char process_name[256];
    char cwd[256];
    char file_path[256];
    int  ret = 0;

    pthread_t reader;

    memset(process_name, 0, 256);
    memset(cwd, 0, 256);
    getcwd(cwd, 256);
    strcpy(process_name, basename(argv[0]));

	logmsg(stdout, "Started. process name[%s] cwd[%s]", process_name, cwd);

    snprintf(file_path, 256, "../../etc/c/%s.conf", process_name);
    logmsg(stdout, "Reading config file[%s]", file_path);
    ret = read_config(file_path);
    if (ret) {
        logmsg(stdout, "Reading config file failed, exiting...");
        return 1;
    }

    snprintf(file_path, 256, "../../etc/c/%s_decode.conf", process_name);
    logmsg(stdout, "Reading decode file[%s]", file_path);
    ret = read_decode_map(file_path);
    if (ret) {
        logmsg(stdout, "Reading decode file failed, exiting...");
        return 1;
    }

    // exit file
    snprintf(g_exitflag_file, 256, "../../flag/%s.exitflag", process_name);
    logmsg(stdout, "Exit flag file[%s]", g_exitflag_file);

    // init thread
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

    // init context
    init_context();

    // start all thread
    ret = pthread_create(&reader, &attr, read_file_thread, NULL);
    if (ret) return ret;

    logmsg(stdout, "All thread started");

    pthread_join(reader, NULL);

    logmsg(stdout, "All thread exited clean");

    return 0;
}
