#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include "utils.h"

sync_info_mem_store *sync_list_head = NULL;
int log_fd = -1;

void load_config_file(const char *config_path) {
    int fd = open(config_path, O_RDONLY);
    if (fd == -1) {
        perror("open config_file");
        exit(EXIT_FAILURE);
    }

    char buffer[1024];
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
                    sync_info_mem_store *entry = malloc(sizeof(sync_info_mem_store));
                    if (!entry) {
                        perror("malloc");
                        close(fd);
                        exit(EXIT_FAILURE);
                    }
                    strncpy(entry->source_dir, src, sizeof(entry->source_dir));
                    strncpy(entry->target_dir, tgt, sizeof(entry->target_dir));
                    entry->active = 1;
                    entry->error_count = 0;
                    entry->last_sync_time = time(NULL);
                    entry->next = sync_list_head;
                    sync_list_head = entry;

                    char logbuf[512];
                    int len = snprintf(logbuf, sizeof(logbuf), "Loaded pair: %s -> %s\n", src, tgt);
                    write(log_fd, logbuf, len);
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
void handle_command(const char *cmd, int pipe_out_fd) {
    char response[512];
    char log_entry[512];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", tm_info);

    if (strncmp(cmd, "add ", 4) == 0) {
        char src[256], tgt[256];
        if (sscanf(cmd + 4, "%255s %255s", src, tgt) == 2) {
            //τσέκαρε αν είναι monitored
            sync_info_mem_store *curr = sync_list_head;
            while (curr) {
                if (strcmp(curr->source_dir, src) == 0 &&
                    strcmp(curr->target_dir, tgt) == 0) {
                    snprintf(response, sizeof(response),
                             "%s Already in queue: %s\n", timebuf, src);
                    write(pipe_out_fd, response, strlen(response));
                    return;
                }
                curr = curr->next;
            }

            //add
            sync_info_mem_store *new_entry = malloc(sizeof(sync_info_mem_store));
            strncpy(new_entry->source_dir, src, 256);
            strncpy(new_entry->target_dir, tgt, 256);
            new_entry->active = 1;
            new_entry->error_count = 0;
            new_entry->last_sync_time = now;
            new_entry->next = sync_list_head;
            sync_list_head = new_entry;

            snprintf(response, sizeof(response),
                     "%s Added directory: %s -> %s\n"
                     "%s Monitoring started for %s\n",
                     timebuf, src, tgt, timebuf, src);
            write(pipe_out_fd, response, strlen(response));

            //log to file
            snprintf(log_entry, sizeof(log_entry),
                     "%s Added directory: %s -> %s\n"
                     "%s Monitoring started for %s\n",
                     timebuf, src, tgt, timebuf, src);
            write(log_fd, log_entry, strlen(log_entry));
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
    
}

void perform_initial_sync(const char *src, const char *dst) {


}