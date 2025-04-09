#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include "utils.h" 

#define PIPE_IN "fss_in"
#define PIPE_OUT "fss_out"

#define MAX_CMD_LEN 256

void print_prompt() {
    printf("fss> ");
    fflush(stdout);
}

void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s -l <console-logfile>\n", progname);
}

int main(int argc, char *argv[]) {
    char *console_logfile = NULL;
    int opt;

    // Ανάγνωση ορίσματος -l
    while ((opt = getopt(argc, argv, "l:")) != -1) {
        switch (opt) {
            case 'l':
                console_logfile = optarg;
                break;
            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (!console_logfile) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    //άνοιγμα cosnole logfile
    int log_fd = open(console_logfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd == -1) {
        perror("open log file");
        exit(EXIT_FAILURE);
    }

    int pipe_in_fd = open(PIPE_IN, O_WRONLY);
    if (pipe_in_fd == -1) {
        perror("open fss_in");
        exit(EXIT_FAILURE);
    }

    int pipe_out_fd = open(PIPE_OUT, O_RDONLY);
    if (pipe_out_fd == -1) {
        perror("open fss_out");
        close(pipe_in_fd);
        exit(EXIT_FAILURE);
    }

    char command[MAX_CMD_LEN];

    while (1) {
        print_prompt();
        if (!fgets(command, MAX_CMD_LEN, stdin)) {
            break;
        }

        
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "exit") == 0) {
            break;
        }

        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        char log_entry[1024];
        
        //ελέγχω αν είναι σωστή εντολή
        if (strncmp(command, "add ", 4) != 0 &&
        strncmp(command, "status ", 7) != 0 &&
        strncmp(command, "sync ", 5) != 0 &&
        strncmp(command, "cancel ", 7) != 0 &&
        strcmp(command, "shutdown") != 0) {
        break; 
    }

    //ελέγχω arguments για να μην κρεμάει
    if (strncmp(command, "add ", 4) == 0) {
        char src[256], trg[256];
        if (sscanf(command + 4, "%255s %255s", src, trg) != 2) {
            printf("Usage: add <source_dir> <target_dir>\n");
            int len = snprintf(log_entry, sizeof(log_entry), "%s Invalid add usage: %s\n", timestamp, command);
            write(log_fd, log_entry, len);
            continue;
        }
    } else if (strncmp(command, "status ", 7) == 0 || strncmp(command, "sync ", 5) == 0 || strncmp(command, "cancel ", 7) == 0) {
        char src[256];
        if (sscanf(strchr(command, ' ') + 1, "%255s", src) != 1) {
            printf("Usage: %s <source_dir>\n", strtok(command, " "));
            int len = snprintf(log_entry, sizeof(log_entry), "%s Invalid usage: %s\n", timestamp, command);
            write(log_fd, log_entry, len);
            continue;
        }
    }

    // Log command
    int len = snprintf(log_entry, sizeof(log_entry), "%s Command %s\n", timestamp, command);
    write(log_fd, log_entry, len);


        //αποστολή σε fss_manager
        if (write(pipe_in_fd, command, strlen(command)) == -1) {
            perror("write to fss_in");
            break;
        }

        //απάντηση από fss_manager
        char response[1024];
        ssize_t bytes_read = read(pipe_out_fd, response, sizeof(response) - 1);
        if (bytes_read > 0) {
            response[bytes_read] = '\0';
            printf("%s\n", response);
            
            int len = snprintf(log_entry, sizeof(log_entry), "%s\n", response);
            write(log_fd, log_entry, len);


            // αν είναι shutdown, τερμάτισε αφού διαβάσεις την απάντηση
            if (strncmp(command, "shutdown", 8) == 0) {
                break;
            }
        } else {
            printf("No response from manager.\n");
            if (strncmp(command, "shutdown", 8) == 0) {
                break;
            }
        }
    }

    close(pipe_in_fd);
    close(pipe_out_fd);
    close(log_fd);
    return 0;
}
