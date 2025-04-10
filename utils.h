#ifndef UTILS_H
#define UTILS_H

#include <time.h>
#include <sys/inotify.h>

#define PIPE_IN "fss_in"
#define PIPE_OUT "fss_out"
#define EVENT_BUF_LEN (1024 * (sizeof(struct inotify_event) + 16))
#define MAX_WORKERS 10
#define MAX_TASK_QUEUE 100

typedef struct sync_info_mem_store {
    char source_dir[256];
    char target_dir[256];
    int active;
    int error_count;
    int is_syncing; 
    int watch_descriptor; 
    pid_t running_worker_pid;
    time_t last_sync_time;
    struct sync_info_mem_store *next;
} sync_info_mem_store;

//λειτουργίες συγχρονισμού
typedef enum {
    OP_FULL,
    OP_ADDED,
    OP_MODIFIED,
    OP_DELETED
} Operation;

typedef struct {
    pid_t pid;
    int pipe_write;
    int pipe_read;
    char src[256];
    char dst[256];
    char filename[256];
    Operation op;
} ActiveWorker;

typedef struct {
    char src[256];
    char dst[256];
    char filename[256];
    Operation op;
} WorkerTask;

extern sync_info_mem_store *sync_list_head;
extern int log_fd;
extern int global_inotify_fd;


void cleanup_previous_state(const char *logfile);

void free_sync_list();
void close_log_file();

int handle_command(const char *cmd, int pipe_out_fd, int pipe_in_fd, int log_fd, int inotify_fd);
void get_timestamp(char *buffer, size_t size);

void load_config_file(const char *config_path, int inotify_fd, int log_fd, int fd_out);
int perform_initial_sync(const char *src, const char *dst, int log_fd);
int add_watch_entry(int inotify_fd, const char *source, const char *target, int log_fd, int fd_out);
void remove_watch_entry(const char *src_dir, int inotify_fd, int log_fd, int pipe_out_fd);
void handle_inotify_events(int inotify_fd, int log_fd);
int sync_on_change(const char *src, const char *dst, int log_fd);

Operation parse_operation(const char *op_str);
void do_full_sync(const char *src, const char *dst, 
    int *files_copied, int *files_skipped, 
    char *error_buffer, size_t *error_offset);
void handle_added(const char *src_dir, const char *dst_dir, const char *filename,
        int *files_copied, int *files_skipped,
        char *error_buffer, size_t *error_offset);

void handle_modified(const char *src_dir, const char *dst_dir, const char *filename,
           int *files_copied, int *files_skipped,
           char *error_buffer, size_t *error_offset);

void handle_deleted(const char *dst_dir, const char *filename,
          int *files_copied, int *files_skipped,
          char *error_buffer, size_t *error_offset);

          void send_exec_report(const char *status, int copied, int skipped, const char *error_buffer);

void log_error(const char *path, const char *msg, char *buffer, size_t *offset);

int start_worker(const char *src, const char *dst, const char *filename, Operation op, pid_t *pid);

void remove_worker_by_pid(pid_t pid);
#endif //UTILS_H
