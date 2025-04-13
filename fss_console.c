#include <stdio.h>
#include <stdlib.h>          // για exit
#include <string.h>         //για strcspn
#include <unistd.h>         //για low I/0
#include <getopt.h>       //για χρήση optarg
#include <errno.h>        //για χρήση errno
#include <sys/select.h>  //για χρήση select
#include <sys/time.h>   //για timestamp
#include <fcntl.h>      //για τις σημαίες
#include "utils.h"    //βιβλιοθήκη με όλες τις απαραίτητες συναρτήσεις

#define PIPE_IN "fss_in"
#define PIPE_OUT "fss_out"

#define MAX_CMD_LEN 256

//συνάρτηση προσομείωσης τερματικού
void print_prompt() {
    printf("fss> ");
    fflush(stdout);
}

//συνάρτηση καταγραφής σφάλματος
void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s -l <console-logfile>\n", progname);
}

//κυρίως συνάρτηση
int main(int argc, char *argv[]) {
    char *console_logfile = NULL;
    int opt;

    //ανάγνωση ορίσματος -l από ./fss_console -l <console-logfile>
    while ((opt = getopt(argc, argv, "l:")) != -1) {
        switch (opt) {
            case 'l':
                console_logfile = optarg;
                break;
            default:
                print_usage(argv[0]); //κλήση συνάρτησης σφάλματος
                exit(EXIT_FAILURE);
        }
    }

    //αν δεν δοθεί το όρισμα επέστρεψε την συνάρτηση σφάλματος
    if (!console_logfile) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    //άνοιγμα console_logfile με δικαιώματα rw -r -r
    int log_fd = open(console_logfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd == -1) {
        perror("open log file");
        exit(EXIT_FAILURE);
    }

    int pipe_in_fd; 
    int attempts = 0;
    //άνοιγμα pipe για γράψιμο χωρίς μπλοκάρισμα
    while ((pipe_in_fd = open(PIPE_IN, O_WRONLY | O_NONBLOCK)) == -1) {
        if (errno == ENXIO && attempts++ < 5) { //ΕΝΧΙΟ=no such device or address
            sleep(1);
            continue;
        }
        perror("open fss_in");
        exit(EXIT_FAILURE);
    }

    //άνοιγμα pipe για διάβασμα 
    int pipe_out_fd = open(PIPE_OUT, O_RDONLY | O_NONBLOCK);
    if (pipe_out_fd == -1) {
        perror("open fss_out");
        close(pipe_in_fd);
        exit(EXIT_FAILURE);
    }

    char command[MAX_CMD_LEN];
    while (1) {
        print_prompt(); //καλεί την συνάρτηση τερματικού
        if (!fgets(command, MAX_CMD_LEN, stdin)) {
            break;
        }

        //εκεί που βρίσκει αλλαγή γραμμής βάζει \0 -string complement span
        command[strcspn(command, "\n")] = '\0'; //μετρά πόσοι αρχικοί δεν ταιριάζουν

        //με exit βγαίνει (μόνο) από τον console -string compare
        if (strcmp(command, "exit") == 0) {
            break;
        }

        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp)); //συνάρτηση στο utils
        char log_entry[1024];
        
        //ελέγχω αν είναι σωστή εντολή
        if (strncmp(command, "add ", 4) != 0 &&
            strncmp(command, "status ", 7) != 0 &&
            strncmp(command, "sync ", 5) != 0 &&
            strncmp(command, "cancel ", 7) != 0 &&
            strcmp(command, "shutdown") != 0) {
            continue; 
        }

        //ελέγχω arguments για να μην κρεμάει
        if (strncmp(command, "add ", 4) == 0) {
            char src[256], trg[256]; //διαβάζω από τον 5ο χαρακτήρα
            if (sscanf(command + 4, "%255s %255s", src, trg) != 2) {
                printf("Usage: add <source_dir> <target_dir>\n");
                //γράφω στον buffer log_entry
                int len = snprintf(log_entry, sizeof(log_entry), "%s Invalid add usage: %s\n", timestamp, command);
                write(log_fd, log_entry, len);
                continue;
            }
        } else if (strncmp(command, "status ", 7) == 0 || strncmp(command, "sync ", 5) == 0 || strncmp(command, "cancel ", 7) == 0) {
            char src[256]; //βρίσκω και προσπερνάω το κενό
            if (sscanf(strchr(command, ' ') + 1, "%255s", src) != 1) {
                //strtok για να  κόψω στο πρώτο κενό
                printf("Usage: %s <source_dir>\n", strtok(command, " "));
                int len = snprintf(log_entry, sizeof(log_entry), "%s Invalid usage: %s\n", timestamp, command);
                write(log_fd, log_entry, len);
                continue;
            }
        }

        //εντολή command που θα φαίνεται στο logfile
        int len = snprintf(log_entry, sizeof(log_entry), "%s Command %s\n", timestamp, command);
        write(log_fd, log_entry, len);

        //flush ώστε να μην περιμένει ο console επ' αόριστον αλλά να δέχεται το μήνυμα
        char flush_buf[1024];
        while (read(pipe_out_fd, flush_buf, sizeof(flush_buf)) > 0);

        //αποστολή σε fss_manager
        if (write(pipe_in_fd, command, strlen(command) + 1) == -1) {
            perror("write to fss_in");
            break;
        }

        //απάντηση από fss_manager
        fd_set read_fds;  //το σύνολο των fds που θα παρακολουθεί το select
        FD_ZERO(&read_fds);  //αρχικοποίηση με κενό
        FD_SET(pipe_out_fd, &read_fds); //προσθέτω pipe_out_fd στο set μου

        int ready = select(pipe_out_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready == -1) { //για να παρακολουθει το set για αλλαγές με NULL 
            perror("select");  //για write, exceptions και timeout
            break;
        }

        if (FD_ISSET(pipe_out_fd, &read_fds)) { //όταν είμαι μέσα στο set
            char response[8192];                //διαβάζω χωρίς κίνδυνο
            ssize_t bytes_read = read(pipe_out_fd, response, sizeof(response) - 1);
            if (bytes_read > 0) {  //-1 για \0 null terminator
                response[bytes_read] = '\0';
                printf("%s\n", response);
                //γράφω στον buffer για να εκτυπώσω
                int len = snprintf(log_entry, sizeof(log_entry), "%s\n", response);
                write(log_fd, log_entry, len);
                if (strncmp(command, "shutdown", 8) == 0) {
                    break;
                }
            } else {
                printf("No response from manager.\n");
            }
        }
      
    }
    //κλείσιμο pipes για να μην έχω .nfs τρεχούμενες διεργασίες
    close(pipe_in_fd);
    close(pipe_out_fd);
    close(log_fd);
    return 0;
}
