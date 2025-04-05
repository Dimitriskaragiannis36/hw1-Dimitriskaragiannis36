#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "utils.h" 

#define DEFAULT_WORKER_LIMIT 5
#define PIPE_IN "fss_in"  
#define PIPE_OUT "fss_out"

void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> -n <worker_limit>\n", progname);
}

int main(int argc, char *argv[]) {
    char *manager_logfile = NULL;
    char *config_file = NULL;
    int worker_limit = DEFAULT_WORKER_LIMIT;

    int opt;
    while ((opt = getopt(argc, argv, "l:c:n:")) != -1) {
        switch (opt) {
            case 'l':
                manager_logfile = optarg;
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'n':
                worker_limit = atoi(optarg);
                break;
            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (!manager_logfile || !config_file) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    // καθαρίζω logfiles και named pipes
    cleanup_previous_state(manager_logfile);

    //ανοίγω manager_logfile
    int log_fd = open(manager_logfile, O_WRONLY | O_APPEND | O_CREAT, 0644);  
    if (log_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    const char *message = "Debug fss_manager started\n";
    ssize_t bytes_written = write(log_fd, message, strlen(message));
    if (bytes_written == -1) {
        perror("write fss_manager problem");
        close(log_fd);
        exit(EXIT_FAILURE);
    }


    //δημιουργώ named pipes
    if (mkfifo(PIPE_IN, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo fss_in");
        close(log_fd);
        exit(EXIT_FAILURE);
    }

    if (mkfifo(PIPE_OUT, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo fss_out");
        close(log_fd);
        exit(EXIT_FAILURE);
    }

    //φορτώνω το config_file στην λίστα utils.h
    load_config_file(config_file);
    
    //ανοίγω named pipes
    int fd_in = open(PIPE_IN, O_RDONLY);
    if (fd_in == -1) {
        perror("open PIPE_IN (manager)");
        exit(EXIT_FAILURE);
    }

    int fd_out = open(PIPE_OUT, O_WRONLY);
    if (fd_out == -1) {
        perror("open PIPE_OUT (manager)");
        close(fd_in);
        exit(EXIT_FAILURE);
    }


    char buffer[256];
    while (1) {
        ssize_t n = read(fd_in, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';

            // Αφαιρούμε new line
            buffer[strcspn(buffer, "\n")] = '\0';

            // Debug log
            dprintf(log_fd, "Received command: %s\n", buffer);

            if (strcmp(buffer, "status") == 0) {
                sync_info_mem_store *curr = sync_list_head;
                char outbuf[1024];
                int len = 0;
                while (curr) {
                    len += snprintf(outbuf + len, sizeof(outbuf) - len,
                                    "Pair: %s -> %s | Errors: %d | Active: %d\n",
                                    curr->source_dir, curr->target_dir,
                                    curr->error_count, curr->active);
                    curr = curr->next;
                }
                if (len == 0)
                    snprintf(outbuf, sizeof(outbuf), "No sync pairs loaded.\n");
                write(fd_out, outbuf, strlen(outbuf));
            } else if (strcmp(buffer, "help") == 0) {
                const char *help_msg =
                    "Available commands:\n"
                    "  status     - Show current sync status\n"
                    "  help       - Show this message\n"
                    "  quit       - Exit console (console side only)\n";
                write(fd_out, help_msg, strlen(help_msg));
            } else if (strcmp(buffer, "shutdown") == 0) {
                const char *msg = "Manager shutting down.\n";
                write(fd_out, msg, strlen(msg));
                break; 
            } else {
                const char *err = "Unknown command. Type 'help' for options.\n";
                write(fd_out, err, strlen(err));
            }
            
        }
    }
    close(fd_in);
    close(fd_out);
    unlink(PIPE_IN);
    unlink(PIPE_OUT);
    close(log_fd);
    return 0;
}
