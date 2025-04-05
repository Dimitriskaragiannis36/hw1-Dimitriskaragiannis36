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
    unlink(PIPE_IN);
    unlink(PIPE_OUT);

    //καθαρίζω αρχείο - το κάνω κενό με truncate
    int fd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("logfile cleanup");
        exit(EXIT_FAILURE);
    }
    close(fd);
}
