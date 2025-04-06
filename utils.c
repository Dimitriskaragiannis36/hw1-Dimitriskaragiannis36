#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <sys/inotify.h>
#include "utils.h"

#define READ_BUFFER_SIZE 1024

sync_info_mem_store *sync_list_head = NULL;
int log_fd = -1;
int global_inotify_fd = -1;


void load_config_file(const char *config_path, int inotify_fd, int log_fd, int fd_out) {
    int fd = open(config_path, O_RDONLY);
    if (fd == -1) {
        perror("open config_file");
        exit(EXIT_FAILURE);
    }

    char buffer[READ_BUFFER_SIZE];
    ssize_t bytes_read;
    size_t total = 0;
    char line[512];
    int line_pos = 0;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; ++i) {
            if (buffer[i] == '\n') {
                line[line_pos] = '\0';
                line_pos = 0;

                char src[256], tgt[256];
                if (sscanf(line, "%255s %255s", src, tgt) == 2) {
                    add_watch_entry(inotify_fd, src, tgt, log_fd, fd_out);
                }
            } else if (line_pos < (int)sizeof(line) - 1) {
                line[line_pos++] = buffer[i];
            }
        }
    }

    if (bytes_read == -1) {
        perror("read config_file");
        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);
}

void cleanup_previous_state(const char *logfile) {
    //καθαρίζω τα named pipes

    if (unlink(PIPE_IN) == -1 && errno != ENOENT) {
        perror("Error unlinking PIPE_IN");
    }
    if (unlink(PIPE_OUT) == -1 && errno != ENOENT) {
        perror("Error unlinking PIPE_OUT");
    }
    

    //καθαρίζω αρχείο - το κάνω κενό με truncate
    int fd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("logfile cleanup");
        exit(EXIT_FAILURE);
    }
    close(fd);
}

void free_sync_list() {
    sync_info_mem_store *current = sync_list_head;
    while (current != NULL) {
        sync_info_mem_store *next = current->next;
        free(current);
        current = next;
    }
}

void close_log_file() {
    if (log_fd != -1) {
        close(log_fd);
        log_fd = -1;
    }
}

int handle_command(const char *cmd, int pipe_out_fd, int pipe_in_fd, int log_fd, int inotify_fd)
 {
    char response[1024]; 
    char log_entry[1024];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[64];
    char last_sync_buf[64];
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", tm_info);

    if (strncmp(cmd, "add ", 4) == 0) {
        char src[256], tgt[256];
        if (sscanf(cmd + 4, "%255s %255s", src, tgt) == 2) {
            int result = add_watch_entry(inotify_fd, src, tgt, log_fd, pipe_out_fd);
            return result;  // απλό και καθαρό
        }//άκυρο
    } else if (strncmp(cmd, "cancel ", 7) == 0) {
        char src[256];
        sscanf(cmd + 7, "%255s", src);
        sync_info_mem_store *curr = sync_list_head;
        int found = 0;
        while (curr) {
            if (strcmp(curr->source_dir, src) == 0) {
                curr->active = 0;
                found = 1;
                snprintf(response, sizeof(response),
                         "%s Monitoring stopped for %s\n", timebuf, src);
                write(pipe_out_fd, response, strlen(response));
                snprintf(log_entry, sizeof(log_entry),
                         "%s Monitoring stopped for %s\n", timebuf, src);
                write(log_fd, log_entry, strlen(log_entry));
                break;
            }
            curr = curr->next;
        }
        if (!found) {
            snprintf(response, sizeof(response),
                     "%s Directory not monitored: %s\n", timebuf, src);
            write(pipe_out_fd, response, strlen(response));
        }
    }
    else if (strncmp(cmd, "status ", 7) == 0) {
        char src[256];
        sscanf(cmd + 7, "%255s", src);
        sync_info_mem_store *curr = sync_list_head;
        int found = 0;
        while (curr) {
            if (strcmp(curr->source_dir, src) == 0) {
                found = 1;
                strftime(last_sync_buf, sizeof(last_sync_buf), "%Y-%m-%d %H:%M:%S", localtime(&curr->last_sync_time));
                snprintf(response, sizeof(response),
                         "%s Status requested for %s\n"
                         "Directory: %s\n"
                         "Target: %s\n"
                         "Last Sync: %s\n"
                         "Errors: %d\n"
                         "Status: %s\n",
                         timebuf, src,
                         curr->source_dir,
                         curr->target_dir,
                         last_sync_buf,
                         curr->error_count,
                         curr->active ? "Active" : "Inactive");
                write(pipe_out_fd, response, strlen(response));
                break;
            }
            curr = curr->next;
        }
        if (!found) {
            snprintf(response, sizeof(response),
                     "%s Directory not monitored: %s\n", timebuf, src);
            write(pipe_out_fd, response, strlen(response));
        }
    }
    else if (strncmp(cmd, "sync ", 5) == 0) {
        char src[256];
        sscanf(cmd + 5, "%255s", src);
        sync_info_mem_store *curr = sync_list_head;
        int found = 0;
        while (curr) {
            if (strcmp(curr->source_dir, src) == 0) {
                found = 1;
                if (curr->is_syncing) {
                    snprintf(response, sizeof(response),
                             "%s Sync already in progress %s\n", timebuf, src);
                    write(pipe_out_fd, response, strlen(response));
                } else {
                    //δοκιμαστικό sync
                    curr->is_syncing = 1;
                    snprintf(response, sizeof(response),
                             "%s Syncing directory: %s -> %s\n", timebuf, curr->source_dir, curr->target_dir);
                    write(pipe_out_fd, response, strlen(response));
                    snprintf(log_entry, sizeof(log_entry),
                             "%s Syncing directory: %s -> %s\n", timebuf, curr->source_dir, curr->target_dir);
                    write(log_fd, log_entry, strlen(log_entry));
    
                    //προσομοίωση χρόνου sync
                    sleep(2);  
                    curr->last_sync_time = time(NULL);
                    curr->is_syncing = 0;
    
                    now = time(NULL);
                    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                    snprintf(response, sizeof(response),
                             "%s Sync completed %s -> %s Errors:%d\n",
                             timebuf, curr->source_dir, curr->target_dir, curr->error_count);
                    write(pipe_out_fd, response, strlen(response));
                    snprintf(log_entry, sizeof(log_entry),
                             "%s Sync completed %s -> %s Errors:%d\n",
                             timebuf, curr->source_dir, curr->target_dir, curr->error_count);
                    write(log_fd, log_entry, strlen(log_entry));
                }
                break;
            }
            curr = curr->next;
        }
        if (!found) {
            snprintf(response, sizeof(response),
                     "%s Directory not monitored: %s\n", timebuf, src);
            write(pipe_out_fd, response, strlen(response));
        }
    }
    else if (strncmp(cmd, "shutdown", 8) == 0) {
        //στέλνει μόνο στην οθόνη (fss_out)
        snprintf(response, sizeof(response),
                 "%s Shutting down manager...\n"
                 "%s Waiting for all active workers to finish.\n"
                 "%s Processing remaining queued tasks.\n",
                 timebuf, timebuf, timebuf);
        write(pipe_out_fd, response, strlen(response));
    
        
        
        //sleep(1); 
        //sleep(1); 
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
        snprintf(response, sizeof(response),
                 "%s Manager shutdown complete.\n", timebuf);
        write(pipe_out_fd, response, strlen(response));

        return 1;
    }
    
return 0;  
}

void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", tm_info);
}

int perform_initial_sync(const char *src, const char *dst) {

    printf("Initial sync: %s -> %s\n", src, dst);
    return 0;
}

sync_info_mem_store* find_entry_by_watch(int wd) {
    sync_info_mem_store *curr = sync_list_head;
    while (curr) {
        if (curr->watch_descriptor == wd) return curr;
        curr = curr->next;
    }
    return NULL;
}

int sync_on_change(const char *src, const char *dst, int log_fd) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Sync triggered: %s -> %s", src, dst);
    write(log_fd, msg, strlen(msg)); 
    return perform_initial_sync(src, dst);
}

void handle_inotify_events(int inotify_fd, int log_fd) {
    char buffer[EVENT_BUF_LEN];
    int length = read(inotify_fd, buffer, EVENT_BUF_LEN);
    if (length < 0) return;

    int i = 0;
    while (i < length) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        if (event->mask & (IN_CREATE | IN_MODIFY | IN_DELETE)) {
            sync_info_mem_store *entry = find_entry_by_watch(event->wd);
            if (entry) {
                sync_on_change(entry->source_dir, entry->target_dir, log_fd);
            }
        }
        i += sizeof(struct inotify_event) + event->len;
    }
}

int add_watch_entry(int inotify_fd, const char *source, const char *target, int log_fd, int fd_out) {
    sync_info_mem_store *curr = sync_list_head;
    while (curr) {
        if (strcmp(curr->source_dir, source) == 0) {
            if (strcmp(curr->target_dir, target) == 0) {
                if (!curr->active) {
                    curr->active = 1;
                }
                char msg[512];
                time_t now = time(NULL);
                struct tm *timeinfo = localtime(&now);
                char time_str[64];
                strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                snprintf(msg, sizeof(msg), "%s Already in queue: %s\n", time_str, source);
                write(fd_out, msg, strlen(msg));
                return 0;
            } else {
                char msg[512];
                time_t now = time(NULL);
                struct tm *timeinfo = localtime(&now); 
                char time_str[64];
                strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                snprintf(msg, sizeof(msg), "%s Source %s already monitored with different target\n", time_str, source);
                write(fd_out, msg, strlen(msg));
                return -1;
            }
        }
        curr = curr->next;
    }

    int wd = inotify_add_watch(inotify_fd, source, IN_CREATE | IN_MODIFY | IN_DELETE);
    if (wd < 0) {
        perror("inotify_add_watch");
        return -1;
    }

    sync_info_mem_store *new_entry = malloc(sizeof(sync_info_mem_store));
    if (!new_entry) {
        perror("malloc");
        return -1;
    }
    strcpy(new_entry->source_dir, source);
    strcpy(new_entry->target_dir, target);
    new_entry->watch_descriptor = wd;
    new_entry->last_sync_time = time(NULL);
    new_entry->next = sync_list_head;
    sync_list_head = new_entry;

    perform_initial_sync(source, target);

    char msg[512];
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now); 
    char time_str[64];
    strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S]", timeinfo);
    snprintf(msg, sizeof(msg), "%s Added directory: %s -> %s\n%s Monitoring started for %s\n",
    time_str, source, target, time_str, source);

    write(log_fd, msg, strlen(msg));          
    write(fd_out, msg, strlen(msg));    

    return 0;
}

Operation parse_operation(const char *op_str) {
    if (strcmp(op_str, "FULL") == 0) return OP_FULL;
    if (strcmp(op_str, "ADDED") == 0) return OP_ADDED;
    if (strcmp(op_str, "MODIFIED") == 0) return OP_MODIFIED;
    if (strcmp(op_str, "DELETED") == 0) return OP_DELETED;

    fprintf(stderr, "Invalid operation: %s\n", op_str);
    exit(EXIT_FAILURE);
}

void handle_added(const char *src, const char *dst, const char *filename) {
    char src_path[512], dst_path[512];
    snprintf(src_path, sizeof(src_path), "%s/%s", src, filename);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, filename);

    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        perror("open src");
        return;
    }

    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        perror("open dst");
        close(src_fd);
        return;
    }

    char buffer[BUF_SIZE];
    ssize_t bytes;
    while ((bytes = read(src_fd, buffer, BUF_SIZE)) > 0) {
        if (write(dst_fd, buffer, bytes) != bytes) {
            perror("write");
            break;
        }
    }

    close(src_fd);
    close(dst_fd);
}

void do_full_sync(const char *src, const char *dst, 
    int *files_copied, int *files_skipped, 
    char *error_buffer, size_t *error_offset) {

    DIR *src_dir = opendir(src);
    if (!src_dir) {
        log_error(src, strerror(errno), error_buffer, error_offset);
        return;
    }

    struct dirent *entry;
    char src_path[512], dst_path[512];

    while ((entry = readdir(src_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

        int src_fd = open(src_path, O_RDONLY);
        if (src_fd < 0) {
            log_error(src_path, strerror(errno), error_buffer, error_offset);
            (*files_skipped)++;
            continue;
        }

        int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dst_fd < 0) {
            perror("open target file");
            log_error(dst_path, strerror(errno), error_buffer, error_offset);
            close(src_fd);
            (*files_skipped)++;
            continue;
        }

        char buffer[BUF_SIZE];
        ssize_t bytes;
        while ((bytes = read(src_fd, buffer, BUF_SIZE)) > 0) {
            if (write(dst_fd, buffer, bytes) != bytes) {
                log_error(dst_path, "write error", error_buffer, error_offset);
                success = 0;
                break;
            }
        }

        if (bytes < 0) {
            log_error(src_path, "read error", error_buffer, error_offset);
            success = 0;
        }

        close(src_fd);
        close(dst_fd);

        if (success)
             (*files_copied)++;
        else
             (*files_skipped)++;
    }

    closedir(src_dir);
}

void handle_modified(const char *src, const char *dst, const char *filename) {
    //απλώς αντικαθιστά το αρχείο
    handle_added(src, dst, filename);
}

void handle_deleted(const char *dst, const char *filename) {
    char dst_path[512];
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, filename);

    if (unlink(dst_path) < 0) {
        perror("unlink");
    }
}

void send_exec_report(const char *status, int copied, int skipped, const char *error_buffer) {
    char line[256];

    write(STDOUT_FILENO, "EXEC_REPORT_START\n", strlen("EXEC_REPORT_START\n"));

    write(STDOUT_FILENO, "STATUS: ", strlen("STATUS: "));
    write(STDOUT_FILENO, status, strlen(status));
    write(STDOUT_FILENO, "\n", 1);

    snprintf(line, sizeof(line), "DETAILS: %d files copied, %d skipped\n", copied, skipped);
    write(STDOUT_FILENO, line, strlen(line));

    if (strlen(error_buffer) > 0) {
        write(STDOUT_FILENO, "ERRORS:\n", strlen("ERRORS:\n"));
        write(STDOUT_FILENO, error_buffer, strlen(error_buffer));
    }

    write(STDOUT_FILENO, "EXEC_REPORT_END\n", strlen("EXEC_REPORT_END\n"));
}


