#ifndef UTILS_H
#define UTILS_H

#include <time.h>

#define PIPE_IN "fss_in"
#define PIPE_OUT "fss_out"

typedef struct sync_info_mem_store {
    char source_dir[256];
    char target_dir[256];
    int active;
    int error_count;
    int is_syncing; 
    time_t last_sync_time;
    struct sync_info_mem_store *next;
} sync_info_mem_store;

extern sync_info_mem_store *sync_list_head;
extern int log_fd;

void load_config_file(const char *config_path);
void cleanup_previous_state(const char *logfile);

void free_sync_list();
void close_log_file();
void handle_command(const char *cmd, int pipe_out_fd, int pipe_in_fd, int log_fd);
void perform_initial_sync(const char *src, const char *dst);
#endif //UTILS_H
