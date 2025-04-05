#ifndef UTILS_H
#define UTILS_H

#include <time.h>

typedef struct sync_info_mem_store {
    char source_dir[256];
    char target_dir[256];
    int active;
    int error_count;
    time_t last_sync_time;
    struct sync_info_mem_store *next;
} sync_info_mem_store;

extern sync_info_mem_store *sync_list_head;
extern int log_fd;

void load_config_file(const char *config_path);
void cleanup_previous_state(const char *logfile);

#endif // UTILS_H
