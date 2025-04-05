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
                logfile = optarg;
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

    if (!logfile || !config_file) {
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
        exit(EXIT_FAILURE)
    }

    //φορτώνω το config_file στην λίστα utils.h
    load_config_file(config_file);
    

    close(log_fd);
    return 0;
}
