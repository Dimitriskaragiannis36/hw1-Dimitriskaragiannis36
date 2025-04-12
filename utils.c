#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <sys/inotify.h>
#include "utils.h"

#define BUF_SIZE 1024

sync_info_mem_store *sync_list_head = NULL;
int log_fd = -1;
int global_inotify_fd = -1;

ActiveWorker active_workers[MAX_WORKERS];
int active_worker_count = 0;

WorkerTask task_queue[MAX_TASK_QUEUE];
int queue_start = 0, queue_end = 0;



void load_config_file(const char *config_path, int inotify_fd, int log_fd, int fd_out) {
    int fd = open(config_path, O_RDONLY);
    if (fd == -1) {
        perror("open config_file");
        exit(EXIT_FAILURE);
    }

    char buffer[BUF_SIZE];
    ssize_t bytes_read;
    char line[1024];
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
            /*if (result == 0) {
                snprintf(response, sizeof(response), "%s Added watch: %s -> %s\n", timebuf, src, tgt);
            } else {
                snprintf(response, sizeof(response), "%s Failed to add watch: %s -> %s\n", timebuf, src, tgt);
            }
            write(pipe_out_fd, response, strlen(response));*/
            return result;
            
        }//άκυρο
    }
    else if (strncmp(cmd, "cancel ", 7) == 0) {
        char src[256];
        if (sscanf(cmd + 7, "%255s", src) == 1) {
            remove_watch_entry(src, inotify_fd, log_fd, pipe_out_fd);
            /*snprintf(log_entry, sizeof(log_entry), "%s Canceled monitoring for %s\n", timebuf, src);
            write(log_fd, log_entry, strlen(log_entry)); */           
        } else {
            snprintf(response, sizeof(response), "%s Invalid cancel command format\n", timebuf);
            write(pipe_out_fd, response, strlen(response));
        }   
    }
    else if (strncmp(cmd, "status ", 7) == 0) {
        char src[256];
        if (sscanf(cmd + 7, "%255s", src) != 1) {
            snprintf(response, sizeof(response), "%s Invalid sync command format\n", timebuf);
            write(pipe_out_fd, response, strlen(response));
            return 0;
        }
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
        if (sscanf(cmd + 5, "%255s", src) != 1) {
            snprintf(response, sizeof(response), "%s Invalid sync command format\n", timebuf);
            write(pipe_out_fd, response, strlen(response));
            return 0;
        }        
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
                    curr->is_syncing = 1;
            
                    char combined_response[2048];
                    now = time(NULL);
                    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                    snprintf(combined_response, sizeof(combined_response),
                            "%s Syncing directory: %s -> %s\n", timebuf, curr->source_dir, curr->target_dir);

                    pid_t pid;
                    int err_count = 0;
                    start_worker(curr->source_dir, curr->target_dir, "ALL", OP_FULL, &pid, &err_count, log_fd);
                    curr->running_worker_pid = pid;
                      
                    curr->last_sync_time = time(NULL);
                    curr->is_syncing = 0;

                    now = time(NULL);
                    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                    snprintf(combined_response + strlen(combined_response), sizeof(combined_response) - strlen(combined_response),
                            "%s Sync completed %s -> %s Errors:%d\n", timebuf, curr->source_dir, curr->target_dir, err_count);

                    //αποστολή του συνδυασμένου μηνύματος
                    write(pipe_out_fd, combined_response, strlen(combined_response));

                    //καταγραφή στο logfile
                    snprintf(log_entry, sizeof(log_entry),
                            "%s Syncing directory: %s -> %s\n", timebuf, curr->source_dir, curr->target_dir);
                    write(log_fd, log_entry, strlen(log_entry));

                    snprintf(log_entry, sizeof(log_entry),
                            "%s Sync completed %s -> %s Errors:%d\n", timebuf, curr->source_dir, curr->target_dir, err_count);
                    write(log_fd, log_entry, strlen(log_entry));
        
                    /*snprintf(response, sizeof(response),
                             "%s Sync completed %s -> %s Errors:%d\n",
                             timebuf, curr->source_dir, curr->target_dir, curr->error_count);
                    snprintf(response, sizeof(response),
                             "%s Sync started %s -> %s\n", timebuf, curr->source_dir, curr->target_dir);
                    write(pipe_out_fd, response, strlen(response));
                    snprintf(log_entry, sizeof(log_entry),
                             "%s Sync completed %s -> %s Errors:%d\n",
                             timebuf, curr->source_dir, curr->target_dir, curr->error_count);
                    snprintf(log_entry, sizeof(log_entry),
                             "%s Sync started %s -> %s\n", timebuf, curr->source_dir, curr->target_dir);                    
                    write(log_fd, log_entry, strlen(log_entry));*/
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
    
        while (active_worker_count > 0) {
            snprintf(response, sizeof(response),
                     "%s Waiting for active workers to finish...\n", timebuf);
            write(pipe_out_fd, response, strlen(response));
            sleep(1); //περιμένουμε λίγο πριν ελέγξουμε ξανά
        } 

        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
        snprintf(response, sizeof(response),
                 "%s Manager shutdown complete.\n", timebuf);
        write(pipe_out_fd, response, strlen(response));

        return 1;
    }
    else {
        snprintf(response, sizeof(response), "%s Unknown command: %s\n", timebuf, cmd);
        write(pipe_out_fd, response, strlen(response));
    }
    return 0;  
}

void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", tm_info);
}

int perform_initial_sync(const char *src, const char *dst, int log_fd) {
    char msg[512];
    time_t now = time(NULL);
    char timebuf[64];

    sync_info_mem_store *entry = sync_list_head;
    while (entry) {
        if (strcmp(entry->source_dir, src) == 0 && strcmp(entry->target_dir, dst) == 0) {
            if (entry->is_syncing) {
                now = time(NULL);
                strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                snprintf(msg, sizeof(msg), "%s Sync already in progress %s\n", timebuf, src);
                write(log_fd, msg, strlen(msg));
                write(STDOUT_FILENO, msg, strlen(msg));
                return 0;
            }

            now = time(NULL);
            strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
            snprintf(msg, sizeof(msg), "%s Syncing directory: %s -> %s\n", timebuf, src, dst);
            write(log_fd, msg, strlen(msg));
            write(STDOUT_FILENO, msg, strlen(msg));

            pid_t pid;
            int err_count = 0;
            int result = start_worker(src, dst, "ALL", OP_FULL, &pid, &err_count, log_fd);
            if (result > 0) {
                entry->is_syncing = 0;
                entry->running_worker_pid = pid;
                now = time(NULL);
                strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                snprintf(msg, sizeof(msg), "%s Sync completed %s -> %s Errors:%d\n", timebuf, src, dst, err_count);
                write(log_fd, msg, strlen(msg));
                write(STDOUT_FILENO, msg, strlen(msg));
                return 0;
            } else {
                snprintf(msg, sizeof(msg), "%s Failed to start worker for %s -> %s\n", timebuf, src, dst);
                write(log_fd, msg, strlen(msg));
                write(STDERR_FILENO, msg, strlen(msg));
                return -1;
            }
        }
        entry = entry->next;
    }

    //δεν βρέθηκε το entry
    snprintf(msg, sizeof(msg), "%s No sync entry for %s -> %s\n", timebuf, src, dst);
    write(log_fd, msg, strlen(msg));
    write(STDERR_FILENO, msg, strlen(msg));
    return -1;
}

sync_info_mem_store* find_entry_by_watch(int wd) {
    sync_info_mem_store *curr = sync_list_head;
    while (curr) {
        if (curr->watch_descriptor == wd) return curr;
        curr = curr->next;
    }
    return NULL;
}

sync_info_mem_store* find_entry_by_source_dir(const char *src) {
    sync_info_mem_store *curr = sync_list_head;
    while (curr) {
        if (strcmp(curr->source_dir, src) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

int sync_on_change(const char *src, const char *dst, int log_fd) {
    char msg[512];
    time_t now = time(NULL);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));

    sync_info_mem_store *entry = sync_list_head;
    while (entry) {
        if (strcmp(entry->source_dir, src) == 0 && strcmp(entry->target_dir, dst) == 0) {
            if (entry->is_syncing) {
                snprintf(msg, sizeof(msg), "%s Sync already in progress %s\n", timebuf, src);
                write(log_fd, msg, strlen(msg));
                printf("%s", msg);
                return 0; //δεν ξεκινά νέο worker
            }

            pid_t pid;
            int err_count = 0;
            time_t now = time(NULL);
            char timebuf[64];
            strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
            
            if (start_worker(src, dst, "", OP_FULL, &pid, &err_count, log_fd) > 0) {
                entry->is_syncing = 0;
                entry->running_worker_pid = pid;

                snprintf(msg, sizeof(msg), "%s Syncing directoryOK3: %s -> %s\n", timebuf, src, dst);
                write(log_fd, msg, strlen(msg));
                write(STDOUT_FILENO, msg, strlen(msg));

                now = time(NULL);
                strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));

                snprintf(msg, sizeof(msg), "%s Sync completed %s -> %s Errors:%d\n", timebuf, src, dst, err_count);
                write(log_fd, msg, strlen(msg));
                write(STDOUT_FILENO, msg, strlen(msg));
                return 0;
            } else {
                snprintf(msg, sizeof(msg), "%s Failed to start worker for: %s -> %s\n", timebuf, src, dst);
                write(log_fd, msg, strlen(msg));
                fprintf(stderr, "%s", msg);
                return -1;
            }
        }
        entry = entry->next;
    }

    //αν δεν βρέθηκε το entry:
    snprintf(msg, sizeof(msg), "%s No sync entry found for %s -> %s\n", timebuf, src, dst);
    write(log_fd, msg, strlen(msg));
    fprintf(stderr, "%s", msg);
    return -1;
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

    new_entry->active = 1;
    new_entry->error_count = 0;
    new_entry->is_syncing = 0;
    new_entry->running_worker_pid = -1;
    
    strncpy(new_entry->source_dir, source, sizeof(new_entry->source_dir));
    strncpy(new_entry->target_dir, target, sizeof(new_entry->target_dir));
    new_entry->watch_descriptor = wd;
    new_entry->last_sync_time = time(NULL);
    new_entry->next = sync_list_head;
    sync_list_head = new_entry;

    char msg[512];
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now); 
    char time_str[64];
    strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S]", timeinfo);
    snprintf(msg, sizeof(msg), "%s Added directory: %s -> %s\n%s Monitoring started for %s\n",
    time_str, source, target, time_str, source);

    write(log_fd, msg, strlen(msg));          
    write(fd_out, msg, strlen(msg));    

    //dprintf(fd_out, "DEBUG: Before perform_initial_sync(%s, %s)\n", source, target);
    perform_initial_sync(source, target, log_fd);
    //dprintf(fd_out, "DEBUG: After perform_initial_sync(%s, %s)\n", source, target);
    new_entry->is_syncing = 0; 
    return 0;
}

void remove_watch_entry(const char *src_dir, int inotify_fd, int log_fd, int pipe_out_fd) {
    char timebuf[64], response[512], log_entry[512];
    time_t now = time(NULL);
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));

    sync_info_mem_store *curr = sync_list_head;
    while (curr) {
        if (strcmp(curr->source_dir, src_dir) == 0) {
            if (!curr->active) {
                snprintf(response, sizeof(response), "%s Directory not monitored: %s\n", timebuf, src_dir);
                write(pipe_out_fd, response, strlen(response));
                return;
            }

            //απενεργοποιούμε το watch
            inotify_rm_watch(inotify_fd, curr->watch_descriptor);
            curr->active = 0;

            snprintf(response, sizeof(response), "%s Monitoring stopped for %s\n", timebuf, src_dir);
            write(pipe_out_fd, response, strlen(response));

            snprintf(log_entry, sizeof(log_entry), "%s Monitoring stopped for %s\n", timebuf, src_dir);
            write(log_fd, log_entry, strlen(log_entry));
            return;
        }
        curr = curr->next;
    }

    //αν δεν βρέθηκε καθόλου
    snprintf(response, sizeof(response), "%s Directory not monitored: %s\n", timebuf, src_dir);
    write(pipe_out_fd, response, strlen(response));
}

Operation parse_operation(const char *op_str) {
    if (strcmp(op_str, "FULL") == 0) return OP_FULL;
    if (strcmp(op_str, "ADDED") == 0) return OP_ADDED;
    if (strcmp(op_str, "MODIFIED") == 0) return OP_MODIFIED;
    if (strcmp(op_str, "DELETED") == 0) return OP_DELETED;

    fprintf(stderr, "Invalid operation: %s\n", op_str);
    exit(EXIT_FAILURE);
}

const char* operation_to_string(Operation op) {
    switch (op) {
        case OP_FULL: return "FULL";
        case OP_ADDED: return "ADDED";
        case OP_MODIFIED: return "MODIFIED";
        case OP_DELETED: return "DELETED";
        default: return "UNKNOWN";
    }
}


void handle_added(const char *src, const char *dst, const char *filename, 
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset) {

    char src_path[512], dst_path[512];
    snprintf(src_path, sizeof(src_path), "%s/%s", src, filename);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, filename);

    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        log_error(src_path, strerror(errno), error_buffer, error_offset);
        (*files_skipped)++;
        return;
    }

    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        log_error(dst_path, strerror(errno), error_buffer, error_offset);
        close(src_fd);
        (*files_skipped)++;
        return;
    }

    char buffer[BUF_SIZE];
    ssize_t bytes;
    int success = 1;
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
        
        int success = 1;

        int src_fd = open(src_path, O_RDONLY);
        if (src_fd < 0) {
            log_error(src_path, strerror(errno), error_buffer, error_offset);
            (*files_skipped)++;
            continue;
        }

        int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dst_fd < 0) {
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

void handle_modified(const char *src_dir, const char *dst_dir, const char *filename,
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset) {
    //απλώς αντικαθιστά το αρχείο
    handle_added(src_dir, dst_dir, filename, files_copied, files_skipped, error_buffer, error_offset);
}

void handle_deleted(const char *dst, const char *filename,
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset) {

    char dst_path[512];
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, filename);

    if (unlink(dst_path) < 0) {
        log_error(dst_path, strerror(errno), error_buffer, error_offset);
        (*files_skipped)++;
    } else {
        (*files_copied)++;  
    }
}

void send_exec_report_to_buffer(char *dest_buffer, size_t buffer_size, const char *status, int copied, int skipped, const char *error_buffer) {
    char temp[256];
    snprintf(dest_buffer, buffer_size, "EXEC_REPORT_START\n");

    snprintf(temp, sizeof(temp), "STATUS: %s\n", status);
    strncat(dest_buffer, temp, buffer_size - strlen(dest_buffer) - 1);

    snprintf(temp, sizeof(temp), "DETAILS: %d files copied, %d skipped\n", copied, skipped);
    strncat(dest_buffer, temp, buffer_size - strlen(dest_buffer) - 1);

    if (strlen(error_buffer) > 0) {
        strncat(dest_buffer, "ERRORS:\n", buffer_size - strlen(dest_buffer) - 1);
        strncat(dest_buffer, error_buffer, buffer_size - strlen(dest_buffer) - 1);
    }

    strncat(dest_buffer, "EXEC_REPORT_END\n", buffer_size - strlen(dest_buffer) - 1);
}

void log_error(const char *path, const char *msg, char *buffer, size_t *offset) {
    int written = snprintf(buffer + *offset, BUF_SIZE - *offset,
                           "[%s] %s\n", path, msg);
    if (written > 0) {
        *offset += written;
        if (*offset >= BUF_SIZE)
            *offset = BUF_SIZE - 1;  // για ασφάλεια
    }
}

int start_worker(const char *src, const char *dst, const char *filename, Operation op, pid_t *pid, int *errors, int log_fd) {
    if (active_worker_count >= MAX_WORKERS) {
        //ελέγχω μην ξεπεράσουν το όριο
        int next_end = (queue_end + 1) % MAX_TASK_QUEUE;
        if (next_end == queue_start) {
            fprintf(stderr, "Task queue overflow, dropping task\n");
            return -1;
        }

        //πρόσθεσε στην ουρά
        strncpy(task_queue[queue_end].src, src, sizeof(task_queue[queue_end].src));
        strncpy(task_queue[queue_end].dst, dst, sizeof(task_queue[queue_end].dst));
        strncpy(task_queue[queue_end].filename, filename, sizeof(task_queue[queue_end].filename));
        task_queue[queue_end].op = op;
        queue_end = next_end;
        return 0;
    }

    int to_worker[2], from_worker[2];
    if (pipe(to_worker) == -1 || pipe(from_worker) == -1) {
        perror("pipe");
        return -1;
    }

    *pid = fork();
    if ( *pid == -1) {
        perror("fork");
        return -1;
    }

    if ( *pid == 0) {
        close(to_worker[1]);
        close(from_worker[0]);
    
        
        if (dup2(to_worker[0], STDIN_FILENO) == -1) {
            perror("dup2(STDIN) failed");
            exit(1);
        }
        if (dup2(from_worker[1], STDOUT_FILENO) == -1) {
            perror("dup2(STDOUT) failed");
            exit(1);
        }
        if (dup2(from_worker[1], STDERR_FILENO) == -1) {
            perror("dup2(STDERR) failed");
            exit(1);
        }
        
        close(to_worker[0]);
        close(from_worker[1]);

        char op_str[16];
        switch (op) {
            case OP_FULL: strcpy(op_str, "FULL"); break;
            case OP_ADDED: strcpy(op_str, "ADDED"); break;
            case OP_MODIFIED: strcpy(op_str, "MODIFIED"); break;
            case OP_DELETED: strcpy(op_str, "DELETED"); break;
        }


        //fprintf(stderr, "[start_worker] Forked child (pid=%d), calling exec with args:\n", getpid());
        //fprintf(stderr, "  src = %s\n  dst = %s\n  filename = %s\n  op = %s\n", src, dst, filename, op_str);

        execl("./worker", "./worker", src, dst, filename, op_str, NULL);
        const char *error_msg = "EXEC_FAILED\n";
        write(STDOUT_FILENO, error_msg, strlen(error_msg));
        perror("exec");
        exit(1);
    } else {
        close(to_worker[0]);
        close(from_worker[1]);

        int flags = fcntl(from_worker[0], F_GETFL, 0);
        fcntl(from_worker[0], F_SETFL, flags | O_NONBLOCK);
        
        char buffer[1024] = {0};
        char temp[256];
        int total_read = 0;
        int found_end = 0;
        
        usleep(100000);  //μικρή καθυστέρηση για να ξεκινήσει ο worker
        
        while (1) {
            int n = read(from_worker[0], temp, sizeof(temp) - 1);
            if (n <= 0) break;  // Δεν υπάρχει άλλο διαθέσιμο, pipe είναι non-blocking
        
            temp[n] = '\0';
            if (total_read + n < sizeof(buffer) - 1) {
                strcat(buffer, temp);
                total_read += n;
            }
        
            if (strstr(buffer, "EXEC_REPORT_END") != NULL) {
                found_end = 1;
                break;
            }
        }
        
        if (strstr(buffer, "EXEC_FAILED") != NULL) {
            //fprintf(stderr, "[parent] Detected EXEC_FAILED from worker %d\n", *pid);
            close(to_worker[1]);
            close(from_worker[0]);
            waitpid(*pid, NULL, 0);
            return -1;
        }
        
        int local_errors = 0;

        if (found_end) {
            //fprintf(stderr, "[parent] Received full report from worker:\n%s\n", buffer);
            char *err_line = strstr(buffer, "ERRORS:");
            if (err_line) {
                sscanf(err_line, "ERRORS:%d", &local_errors);
        
            }
            char *status = NULL, *details = NULL;
            char *line = strtok(buffer, "\n");

            while (line) {
                if (strncmp(line, "STATUS:", 7) == 0) {
                    status = line + 7;
                } else if (strncmp(line, "DETAILS:", 7) == 0) {
                    details = line + 8;
                }
                line = strtok(NULL, "\n");
            }

            if (status) while (*status == ' ') status++;
            if (details) while (*details == ' ') details++;

           // if (status && details) {
                time_t now = time(NULL);
                struct tm *timeinfo = localtime(&now);
                char timebuf[64];
                strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                char log_message[1024];
                snprintf(log_message, sizeof(log_message),
                "%s [%s] [%s] [%d] [%s] [%s] [%s]\n",
                timebuf,
                src,
                dst,
                *pid, 
                operation_to_string(op),
                status,
                details);

                write(log_fd, log_message, strlen(log_message));
            /*} else {
                char timebuf[64];
                time_t now = time(NULL);
                struct tm *timeinfo = localtime(&now);
                strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", timeinfo);
        
                char error_message[256];
                snprintf(error_message, sizeof(error_message),
                        "%s Incomplete worker report for file %s\n", timebuf, filename);
        
                write(log_fd, error_message, strlen(error_message));
            }*/
        }

        if (errors) *errors = local_errors;

        sync_info_mem_store *info = find_entry_by_source_dir(src);
        if (info != NULL) {
            info->is_syncing = 0;
        }        

        ActiveWorker *w = &active_workers[active_worker_count++];
        w->pid = *pid;
        w->pipe_write = to_worker[1];
        w->pipe_read = from_worker[0];
        strncpy(w->src, src, sizeof(w->src));
        strncpy(w->dst, dst, sizeof(w->dst));
        strncpy(w->filename, filename, sizeof(w->filename));
        w->op = op;

        return 1;
    }
}

void remove_worker_by_pid(pid_t pid, int log_fd) {
    for (int i = 0; i < active_worker_count; i++) {
        if (active_workers[i].pid == pid) {
            close(active_workers[i].pipe_read);
            close(active_workers[i].pipe_write);

            active_workers[i] = active_workers[--active_worker_count];

            if (queue_start != queue_end) {
                WorkerTask *t = &task_queue[queue_start];
                pid_t new_pid;
                int err_count = 0;
                start_worker(t->src, t->dst, t->filename, t->op, &new_pid, &err_count, log_fd);
                queue_start = (queue_start + 1) % MAX_TASK_QUEUE;
            }
            break;
        }
    }
}
