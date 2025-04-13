#include <stdio.h>
#include <stdlib.h>        //για exit
#include <unistd.h>        //για low I/0 
#include <fcntl.h>       //για τις σημαίες
#include <sys/stat.h>    //για pipes
#include <sys/wait.h>    //για waitpid
#include <errno.h>      //για errno
#include <getopt.h>     //για χρήση optarg
#include <signal.h>    //για sigaction
#include "utils.h"    //βιβλιοθήκη με όλες τις απαραίτητες συναρτήσεις

#define PIPE_IN "fss_in"  
#define PIPE_OUT "fss_out"
#define MAX_CMD_LEN 256
#define DEFAULT_WORKER_LIMIT 5

//έχουν οριστεί στο utils.c
extern ActiveWorker active_workers[];
extern int active_worker_count; 

//συνάρτηση σφάλματος
void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> -n <worker_limit>\n", progname);
}

//πτητική για να μην επηρεάζεται από εξωτερικές αλλαγές
//ατομική για να μην υπάρχουν συνθήκες ανταγωνισμού
volatile sig_atomic_t dead_pids[256];
volatile sig_atomic_t dead_count = 0;

//ο χειριστής σήματος με waitpid
void sigchld_handler(int signo) {
    int status;
    pid_t pid;

    //αποθηκεύει ποιοι workers πέθαναν χωρίς να κρεμάει
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        dead_pids[dead_count++] = pid;
    }
}

//κυρίως συνάρτηση
int main(int argc, char *argv[]) {
    char *manager_logfile = NULL;
    char *config_file = NULL;

    int opt; //ορίσματα με προαιρετικό το -n στο 
    // ./fss_manager -l <manager_logfile> -c <config_file> -n <worker_limit>
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
                print_usage(argv[0]); //κλήση συνάρτησης σφάλματος
                exit(EXIT_FAILURE);
        }
    }

    //έλεγχος αν δόθηκαν τα ορίσματα
    if (!manager_logfile || !config_file) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    //καθαρίζω logfiles και named pipes στο utils
    cleanup_previous_state(manager_logfile);

    //ανοίγω manager_logfile με δικαιώματα rw -r -r
    int log_fd = open(manager_logfile, O_WRONLY | O_APPEND | O_CREAT, 0644);  
    if (log_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    //δημιουργώ named pipes  (χωρίς να με απασχολεί αν υπάρχουν ήδη)
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
   
    //ανοίγω named pipes χωρίς να μπλοκάρουν
    int fd_in = open(PIPE_IN, O_RDONLY | O_NONBLOCK);
    if (fd_in == -1) {
        perror("open PIPE_IN (manager)");
        exit(EXIT_FAILURE);
    }

    int fd_out;
    int attempts = 0;
    while ((fd_out = open(PIPE_OUT, O_WRONLY | O_NONBLOCK)) == -1) {
        if (errno == ENXIO && attempts++ < 5) { //ΕΝΧΙΟ=no such device or address
            sleep(1);
            continue;
        }
        perror("open PIPE_OUT (manager)");
        close(fd_in);
        exit(EXIT_FAILURE);
    }

    //η δομή του sigaction
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; //restart για τα syscalls
                                            //SA_NOCLDSTOP για να αποφύγω το
                                            //προσωρινό σταμάτημα των worker
    //για διαχείριση SIGCHILD
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }    

    //αρχικοποίηση inotify που μου επιστρέφει fd
    int inotify_fd = inotify_init1(0);
    if (inotify_fd < 0) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    //φορτώνω τα ζεύγη από το config και ξεκινάω monitoring/sync
    load_config_file(config_file, inotify_fd, log_fd, fd_out);

    //οι εντολές από fss_console
    char command[MAX_CMD_LEN];

    fd_set fds; //νέο set για select και καθορισμός του μεγίστου
    int max_fd = (fd_in > inotify_fd) ? fd_in : inotify_fd;
    while (1) {
        FD_ZERO(&fds);   //μηδενισμός set
        FD_SET(fd_in, &fds);  //βάζω στο set το fd_in και inotify_fd
        FD_SET(inotify_fd, &fds);
        
        for (int i = 0; i < active_worker_count; i++) {
            FD_SET(active_workers[i].pipe_read, &fds); //βάζω στο set
            if (active_workers[i].pipe_read > max_fd)  //to fd των workers
                max_fd = active_workers[i].pipe_read;
        }

        int sel;      //select για παρακολούθηση αλλαγών
        do {          //NULL για για write, exceptions και timeout
            sel = select(max_fd + 1, &fds, NULL, NULL, NULL);
        } while (sel == -1 && errno == EINTR);  //eπανεκκίνηση αν διακοπεί από σήμα
    
        if (sel == -1) {
            perror("select");
            break;
        }
        
        //αν είναι μέσα στο set το fd_in
        if (FD_ISSET(fd_in, &fds)) {
            ssize_t bytes = read(fd_in, command, sizeof(command) - 1);
            if (bytes <= 0) continue;
            command[bytes] = '\0'; //κλήση συνάρτησης από utils
            if (handle_command(command, fd_out, fd_in, log_fd, inotify_fd)) {
                close(fd_in);  //αν αποτύχει τα κλείνω όλα
                close(fd_out);
                unlink(PIPE_IN);
                unlink(PIPE_OUT);
                close(log_fd);
                break;
            }
        }
        
        //αν είναι μέσα στο set το inotify_fd κλήση συνάρτησης από utils
        if (FD_ISSET(inotify_fd, &fds)) {
            handle_inotify_events(inotify_fd, log_fd);
        }
    
        //διάβασμα EXEC_REPORT από pipe των workers
        for (int i = 0; i < active_worker_count; ) {
            int fd = active_workers[i].pipe_read;
    
            if (FD_ISSET(fd, &fds)) {
                char buffer[1024];
                ssize_t len = read(fd, buffer, sizeof(buffer) - 1);

                if (len > 0) {
                    buffer[len] = '\0'; 
                    write(log_fd, buffer, len); 
        
                }
                close(fd);
        
                //μετακίνησε το τελευταίο στοιχείο εδώ για να διαγράψεις χωρίς να αφήσεις κενό
                active_workers[i] = active_workers[active_worker_count - 1];
                active_worker_count--;
            } else {
                i++; //μόνο αν δεν έγινε ανάγνωση, προχώρα στον επόμενο
            }
        }
    
        //χειρισμός dead_pids από SIGCHLD
        for (int i = 0; i < dead_count; i++) {
            pid_t pid = dead_pids[i];
    
            //βρες τον worker και διάβασε πριν τον αφαιρέσεις
            for (int j = 0; j < active_worker_count; j++) {
                if (active_workers[j].pid == pid) {
                    char buffer[1024];
                    ssize_t len;
    
                    while ((len = read(active_workers[j].pipe_read, buffer, sizeof(buffer) - 1)) > 0) {
                        buffer[len] = '\0';
                        write(log_fd, buffer, len);
                    }
                    //κλήση συνάρτησης στο utils
                    remove_worker_by_pid(pid, log_fd);
                    break;
                }
            }
        }
        dead_count = 0;
    }
    
    return 0;
}
