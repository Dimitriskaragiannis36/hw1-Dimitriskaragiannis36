#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define PIPE_IN "fss_in"
#define PIPE_OUT "fss_out"

#define MAX_CMD_LEN 256

void print_prompt() {
    printf("fss> ");
    fflush(stdout);
}

int main() {
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
    return 0;
}
