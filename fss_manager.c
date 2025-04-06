#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <getopt.h>
#include "utils.h" 

#define DEFAULT_WORKER_LIMIT 5
#define PIPE_IN "fss_in"  
#define PIPE_OUT "fss_out"
#define MAX_CMD_LEN 256

void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> -n <worker_limit>\n", progname);
}

int main(int argc, char *argv[]) {
    char *manager_logfile = NULL;
    char *config_file = NULL;
    int worker_limit;

    int opt;
    while ((opt = getopt(argc, argv, "l:c:n::")) != -1) {
        switch (opt) {
            case 'l':
                manager_logfile = optarg;
                break;
            case 'c':
                config_file = optarg;
                break;
                case 'n':
                if (optarg) {
                    worker_limit = atoi(optarg);
                } else {
                    worker_limit = DEFAULT_WORKER_LIMIT;
                }
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

    //καθαρίζω logfiles και named pipes
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

    //αρχικοποίηση inotify
    int inotify_fd = inotify_init1(0);
    if (inotify_fd < 0) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    //αρχικοί κατάλογοι από config
    load_config_file(config_file);
    sync_info_mem_store *curr = sync_list_head;
    while (curr) {
        add_watch_entry(inotify_fd, curr->source_dir, curr->target_dir, log_fd, fd_out);
        curr = curr->next;
    }


    //οι εντολές από fss_console
    char command[MAX_CMD_LEN];

    fd_set fds;
    int max_fd = (fd_in > inotify_fd) ? fd_in : inotify_fd;
    while (1) {
        FD_ZERO(&fds);
        FD_SET(fd_in, &fds);
        FD_SET(inotify_fd, &fds);
    
        if (select(max_fd + 1, &fds, NULL, NULL, NULL) == -1) {
            perror("select");
            break;
        }
    
        if (FD_ISSET(fd_in, &fds)) {
            ssize_t bytes = read(fd_in, command, sizeof(command) - 1);
            if (bytes <= 0) continue;
            command[bytes] = '\0';
            if (handle_command(command, fd_out, fd_in, log_fd)) {
                close(fd_in);
                close(fd_out);
                unlink(PIPE_IN);
                unlink(PIPE_OUT);
                close(log_fd);
                break;
            }
        }
    
        if (FD_ISSET(inotify_fd, &fds)) {
            handle_inotify_events(inotify_fd, log_fd);
        }
        
    }

    /*αρχικός συγχρονισμός
    sync_info_mem_store *curr = sync_list_head;
    while (curr) {
        pid_t pid = fork();
        if (pid == 0) {
            //παιδί -> worker process
            perform_initial_sync(curr->source_dir, curr->target_dir);
            exit(0);
        }
        //μπαμπάς -> συνεχίζει
        curr = curr->next;
    }*/

}
