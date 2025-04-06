#ifndef UTILS_H
#define UTILS_H

#include <time.h>
#include <sys/inotify.h>

#define PIPE_IN "fss_in"
#define PIPE_OUT "fss_out"
#define EVENT_BUF_LEN (1024 * (sizeof(struct inotify_event) + 16))

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
extern int global_inotify_fd;


void cleanup_previous_state(const char *logfile);

void free_sync_list();
void close_log_file();

int handle_command(const char *cmd, int pipe_out_fd, int pipe_in_fd, int log_fd);
void get_timestamp(char *buffer, size_t size);
void perform_initial_sync(const char *src, const char *dst);

void load_config_file(const char *config_path, int inotify_fd, int log_fd, int fd_out);
int perform_initial_sync(const char *src, const char *dst);
int add_watch_entry(int inotify_fd, const char *source, const char *target, int log_fd, int fd_out);
void handle_inotify_events(int inotify_fd, int log_fd);
int sync_on_change(const char *src, const char *dst, int log_fd);
#endif //UTILS_H
