#include <stdio.h>
#include <stdlib.h>             //για exit
#include <string.h>             //για χρήση strncmp
#include <unistd.h>             //για low I/0
#include <fcntl.h>           //για σημαίες O_CREAT κτλ
#include <sys/types.h>      //για pid_t, ssize_t
#include <sys/wait.h>       //για waitpid
#include <time.h>         //για time_t
#include <errno.h>        //για χρήση errno
#include <dirent.h>       //για opendir, readdir, struct dirent      
#include <sys/inotify.h>  //για struct inotify_event
#include "utils.h"      //βιβλιοθήκη με όλες τις απαραίτητες συναρτήσεις

#define BUF_SIZE 1024
#define DEFAULT_WORKER_LIMIT 5

sync_info_mem_store *sync_list_head = NULL;  //αρχικοποίηση της δομής που βρίσκεται στο utils.h 

ActiveWorker active_workers[MAX_WORKERS]; //αρχικοποίηση πίνακα δομής του utils.h
int active_worker_count = 0;

WorkerTask task_queue[MAX_TASK_QUEUE];  //αρχικοποίηση πίνακα δομής του utils.h
int queue_start = 0, queue_end = 0;

int worker_limit = DEFAULT_WORKER_LIMIT;   //εδώ παίρνει την default (5) τιμή



//--------------------------FSS_MANAGER--------------------------------------------
//συνάρτηση καθαρισμού pipes και logfile
void cleanup_previous_state(const char *logfile) {
    
    //καθαρίζω τα named pipes (ENOENT= υπάρχει ήδη)
    if (unlink(PIPE_IN) == -1 && errno != ENOENT) {
        perror("Error unlinking PIPE_IN");
    }
    if (unlink(PIPE_OUT) == -1 && errno != ENOENT) {
        perror("Error unlinking PIPE_OUT");
    }
    
    //καθαρίζω αρχείο - το κάνω κενό με truncate με δικαιώματα rw -r -r
    int fd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("logfile cleanup");
        exit(EXIT_FAILURE);
    }
    close(fd);
}

//συνάρτηση φόρτωσης ζευγών από το config_file
void load_config_file(const char *config_path, int inotify_fd, int log_fd, int fd_out) {
    //άνοιγμα αρχείου μόνο για διάβασμα
    int fd = open(config_path, O_RDONLY);
    if (fd == -1) {
        perror("open config_file");
        exit(EXIT_FAILURE);
    }

    char buffer[BUF_SIZE];
    ssize_t bytes_read;   //αφού διαβάζουμε bytes
    char line[1024];
    int line_pos = 0;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; ++i) {
            if (buffer[i] == '\n') {  //αν βρω \n βάζω στην γραμμή το κείμενο 
                line[line_pos] = '\0';  //και μετά null terminator
                line_pos = 0;

                char src[256], tgt[256];
                if (sscanf(line, "%255s %255s", src, tgt) == 2) {
                    //κλήση συνάρτησης για παρακολούθηση καταλόγου
                   add_watch_entry(inotify_fd, src, tgt, log_fd, fd_out);
                }
            } else if (line_pos < (int)sizeof(line) - 1) {
                line[line_pos++] = buffer[i]; //αν δεν έχει φτάσει στο \n
            }                           //και δεν έχει ξεπεράσει το 1024
        }                               //βάζει τον χαρακτήρα μέσα
    }
    //σε περίπτωση λάθους
    if (bytes_read == -1) {
        perror("read config_file");
        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);
}

//συνάρτηση χειρισμού command μεταξύ manager με console
int handle_command(const char *cmd, int pipe_out_fd, int pipe_in_fd, int log_fd, int inotify_fd)
 {
    char response[1024]; 
    char log_entry[1024];
    time_t now = time(NULL); //για να πάρω τον χρόνο τώρα
    struct tm *tm_info = localtime(&now); //την μετατρέπει σε δομή ημερομηνίας/ώρας τοπικής ζώνης
    char timebuf[64];
    char last_sync_buf[64];  //formation της ώρας όπως ζητήθηκε 
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", tm_info);

    if (strncmp(cmd, "add ", 4) == 0) {  //συγκρίνει τα 4 πρώτα chr
        char src[256], tgt[256]; //εκεί που είναι το cmd + 4
        if (sscanf(cmd + 4, "%255s %255s", src, tgt) == 2) {
            int result = add_watch_entry(inotify_fd, src, tgt, log_fd, pipe_out_fd);
            //κλήση συνάρτησης για παρακολούθηση καταλόγου
            return result;
            
        }
    }           //η επόμενη εντολή στο fss_console
    else if (strncmp(cmd, "cancel ", 7) == 0) { //συγκρίνει τα πρώτα 7
        char src[256];
        if (sscanf(cmd + 7, "%255s", src) == 1) { //από το cmd 7 δεξιά
            remove_watch_entry(src, inotify_fd, log_fd, pipe_out_fd);
            //κλήση συνάρτησης για ακύρωση παρακολούθησης καταλόγου
        } else {
            //γράψε λάθος
            snprintf(response, sizeof(response), "%s Invalid cancel command format\n", timebuf);
            write(pipe_out_fd, response, strlen(response));
        }   
    }       //η εντολή status
    else if (strncmp(cmd, "status ", 7) == 0) {   //συγκρίνει πάλι τα πρώτα 7
        char src[256];
        if (sscanf(cmd + 7, "%255s", src) != 1) {   //από το cmd 7 δεξιά
            //μήνυμα λάθους
            snprintf(response, sizeof(response), "%s Invalid sync command format\n", timebuf);
            write(pipe_out_fd, response, strlen(response));
            return 0;
        }
        sync_info_mem_store *curr = sync_list_head; //η κεφαλή που αρχικοποιήσαμε πριν
        int found = 0;
        while (curr) {   //η δομή που είχαμε πριν
            if (strcmp(curr->source_dir, src) == 0) {
                found = 1;   //το formation της εκφώνησης
                strftime(last_sync_buf, sizeof(last_sync_buf), "%Y-%m-%d %H:%M:%S", localtime(&curr->last_sync_time));
                snprintf(response, sizeof(response),  //αυτό που ζητείται να εκτυπωθεί
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
            curr = curr->next;  //μετάβαση στο επόμενο της λίστας
        }
        //σε περίπτωση λάθους
        if (!found) {
            snprintf(response, sizeof(response),
                     "%s Directory not monitored: %s\n", timebuf, src);
            write(pipe_out_fd, response, strlen(response));
        }
    }       //η εντολή sync στον console
    else if (strncmp(cmd, "sync ", 5) == 0) {
        char src[256];
        if (sscanf(cmd + 5, "%255s", src) != 1) {  //εκεί που βρίσκεται το cmd + 5
            //μήνυμα λάθους
            snprintf(response, sizeof(response), "%s Invalid sync command format\n", timebuf);
            write(pipe_out_fd, response, strlen(response));
            return 0;
        }        
        sync_info_mem_store *curr = sync_list_head; //πάλι η κορυφή της λίστας
        int found = 0;
        while (curr) { //η δομή λίστας
            if (strcmp(curr->source_dir, src) == 0) {
                found = 1;
                if (curr->is_syncing) { //το συγκεκριμένο στοιχείο
                    snprintf(response, sizeof(response), //αν όντως έχουμε κάπου sync
                             "%s Sync already in progress %s\n", timebuf, src);
                    write(pipe_out_fd, response, strlen(response));
                } else {
                    //αλλιώς το δηλώνω πως συγρονίζεται
                    curr->is_syncing = 1;
            
                    char combined_response[2048]; //για να τα εμφανίσει όλα μαζι
                    now = time(NULL); //η τρεχουσα ώρα και το formation που ζητάμε
                    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                    snprintf(combined_response, sizeof(combined_response),
                            "%s Syncing directory: %s -> %s\n", timebuf, curr->source_dir, curr->target_dir);
                        //πλέον συγχρονίζεται και θα καλέσω την συνάρτηση για αυτό
                    pid_t pid;
                    int err_count = 0;  //συνάρτηση που θα κάνει το fork και το exec 
                    start_worker(curr->source_dir, curr->target_dir, "ALL", OP_FULL, &pid, &err_count, log_fd);
                    curr->running_worker_pid = pid;  //θα επιστρέψει το pid 
                                                    //με call by reference
                    curr->last_sync_time = time(NULL); //ενημερώνουμε την ώρα συγχρονισμού
                    curr->is_syncing = 0; //τέλος συγχρονισμού

                    now = time(NULL); //νέα ώρα και formation
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
                }
                break;
            }
            curr = curr->next; //επόμενο στην λίστα
        }
        //αν δεν βρεθεί, μήνυμα λάθους
        if (!found) {
            snprintf(response, sizeof(response),
                     "%s Directory not monitored: %s\n", timebuf, src);
            write(pipe_out_fd, response, strlen(response));
        }
    }       //τελευταία εντολή 
    else if (strncmp(cmd, "shutdown", 8) == 0) { //συγκρίνει τα 8 πρώτα
        //στέλνει μόνο στην οθόνη (fss_out)
        snprintf(response, sizeof(response),
                 "%s Shutting down manager...\n"
                 "%s Waiting for all active workers to finish.\n"
                 "%s Processing remaining queued tasks.\n",
                 timebuf, timebuf, timebuf);
        write(pipe_out_fd, response, strlen(response));
        
        //σε περίπτωση που έχουμε πάρα πολλούς workers σε εκκρεμότητα
        while (active_worker_count > 0) {
            snprintf(response, sizeof(response),
                     "%s Waiting for active workers to finish...\n", timebuf);
            write(pipe_out_fd, response, strlen(response));
            sleep(1); //περιμένουμε λίγο πριν ελέγξουμε ξανά
        } 

        now = time(NULL); //χρόνος και formation για την σωστή χρονοσφραγίδα
        strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
        snprintf(response, sizeof(response),
                 "%s Manager shutdown complete.\n", timebuf);
        write(pipe_out_fd, response, strlen(response));

        return 1; //1 γιατί αν είναι true θα κλείνει τα pipes στον manager
    }               //και δεν θα κρεμάνε ως .nfs
    else {
        //σε περίτπωση λάθους
        snprintf(response, sizeof(response), "%s Unknown command: %s\n", timebuf, cmd);
        write(pipe_out_fd, response, strlen(response));
    }
    return 0;  
}

//συνάρτηση παρακολούθησης καταλόγου με inotify
int add_watch_entry(int inotify_fd, const char *source, const char *target, int log_fd, int fd_out) {
    sync_info_mem_store *curr = sync_list_head; //η λίστα curr
    while (curr) { //όσο υπάρχει στοιχείο
        if (strcmp(curr->source_dir, source) == 0) { //αν βρει και τα 2
            if (strcmp(curr->target_dir, target) == 0) {
                if (!curr->active) {
                    curr->active = 1;  //το κάνει παρακολουθούμενο
                }
                char msg[512];
                time_t now = time(NULL); //τρέχουσα ώρα 
                struct tm *timeinfo = localtime(&now); //για να έχω πρόσβαση σε όλα τα πεδία
                char time_str[64]; //το τυπικό formation
                strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                snprintf(msg, sizeof(msg), "%s Already in queue: %s\n", time_str, source);
                write(fd_out, msg, strlen(msg));
                return 0;
            } else {
                //αν ήδη χρησιμοποιείται το source και δώσω άλλο target
                char msg[512];
                time_t now = time(NULL);   //χρήση τρέχουσας ώρας
                struct tm *timeinfo = localtime(&now); //για να έχω πρόσβαση σε όλα τα πεδία
                char time_str[64];    //formation
                strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                snprintf(msg, sizeof(msg), "%s Source %s already monitored with different target\n", time_str, source);
                write(fd_out, msg, strlen(msg));
                return -1;
            }
        }
        curr = curr->next;  //πάμε στο επόμενο στοιχεί της λίστας
    }
    //βασική συνάρτηση της inotify για το source που θα παρακολουθώ
    int wd = inotify_add_watch(inotify_fd, source, IN_CREATE | IN_MODIFY | IN_DELETE);
    if (wd < 0) {
        perror("inotify_add_watch");
        return -1;
    }
    //δέσμευση μνήμης για νέα entry
    sync_info_mem_store *new_entry = malloc(sizeof(sync_info_mem_store));
    if (!new_entry) {
        perror("malloc");
        return -1;
    }
    //αρχικοποίηση στοιχείων νέας entry
    new_entry->active = 1;
    new_entry->error_count = 0;
    new_entry->is_syncing = 0;
    new_entry->running_worker_pid = -1;
    //αντιγραφή paths
    strncpy(new_entry->source_dir, source, sizeof(new_entry->source_dir));
    strncpy(new_entry->target_dir, target, sizeof(new_entry->target_dir));
    new_entry->watch_descriptor = wd;
    new_entry->last_sync_time = time(NULL);
    new_entry->next = sync_list_head;       //εισαγωγή νέου κόμβου
    sync_list_head = new_entry;

    char msg[512];
    time_t now = time(NULL); //χρήση τρέχουσας ώρας 
    struct tm *timeinfo = localtime(&now); //χρήση δομής χρόνου
    char time_str[64];      //formation
    strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S]", timeinfo);
    snprintf(msg, sizeof(msg), "%s Added directory: %s -> %s\n%s Monitoring started for %s\n",
    time_str, source, target, time_str, source);

    write(log_fd, msg, strlen(msg));          
    write(fd_out, msg, strlen(msg));    

    //κλήση συνάρτησης αρχικού συγχρονισμού
    perform_initial_sync(source, target, log_fd);
   
    new_entry->is_syncing = 0; //αφού τελειώσει 
    return 0;
}

//συνάρτηση αρχικού συγχρονισμού (από τα ζεύγη του config_file)
int perform_initial_sync(const char *src, const char *dst, int log_fd) {
    char msg[512];
    time_t now = time(NULL); //χρήση της τρέχουσας ώρας
    char timebuf[64];

    sync_info_mem_store *entry = sync_list_head; //χρήση της λίστας με όνομα entry
    while (entry) {         //αν βρώ σωστά τα src αι trg
        if (strcmp(entry->source_dir, src) == 0 && strcmp(entry->target_dir, dst) == 0) {
            if (entry->is_syncing) {        //αν συγχρονίζεται
                now = time(NULL);  //χρήση της τρέχουσας ώρας + formation
                strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                snprintf(msg, sizeof(msg), "%s Sync already in progress %s\n", timebuf, src);
                write(log_fd, msg, strlen(msg));
                write(STDOUT_FILENO, msg, strlen(msg));
                return 0;
            }
            //αλλιώς εκτελεί συγχρονισμό
            now = time(NULL);       //χρήση της τρέχουσας ώρας + formation
            strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
            snprintf(msg, sizeof(msg), "%s Syncing directory: %s -> %s\n", timebuf, src, dst);
            write(log_fd, msg, strlen(msg));
            write(STDOUT_FILENO, msg, strlen(msg));

            pid_t pid;
            int err_count = 0;  //για τον συγχρονισμό καλώ την start_worker
            int result = start_worker(src, dst, "ALL", OP_FULL, &pid, &err_count, log_fd);
            if (result > 0) {
                entry->is_syncing = 0; //οταν το συγχρονίσει το επιστρέφω στο 0
                entry->running_worker_pid = pid; //κρατάω το pid με call by reference
                now = time(NULL);   //χρήση της τρέχουσας ώρας + formation
                strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                snprintf(msg, sizeof(msg), "%s Sync completed %s -> %s Errors:%d\n", timebuf, src, dst, err_count);
                write(log_fd, msg, strlen(msg));
                write(STDOUT_FILENO, msg, strlen(msg));
                return 0;
            } else {
                //αλλιώς μήνυμα σφάλματος
                snprintf(msg, sizeof(msg), "%s Failed to start worker for %s -> %s\n", timebuf, src, dst);
                write(log_fd, msg, strlen(msg));
                write(STDERR_FILENO, msg, strlen(msg));
                return -1;
            }
        }
        entry = entry->next;  //πάμε στο επόμενο στοιχείο της λίστας
    }
    
    //δεν βρέθηκε το entry καθόλου
    snprintf(msg, sizeof(msg), "%s No sync entry for %s -> %s\n", timebuf, src, dst);
    write(log_fd, msg, strlen(msg));
    write(STDERR_FILENO, msg, strlen(msg));
    return -1;
}

//συνάρτηση διακοπής παρακολούθησης καταλόγου με inotify
void remove_watch_entry(const char *src_dir, int inotify_fd, int log_fd, int pipe_out_fd) {
    char timebuf[64], response[512], log_entry[512];
    time_t now = time(NULL); //χρήση τρέχουσας ώρας και formation
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));

    sync_info_mem_store *curr = sync_list_head;  //κορυφή λίστας
    while (curr) {   //μέχρι να περιέχει στοιχεία
        if (strcmp(curr->source_dir, src_dir) == 0) {
            if (!curr->active) { //αν βρει το source μη ενεργό
                snprintf(response, sizeof(response), "%s Directory not monitored: %s\n", timebuf, src_dir);
                write(pipe_out_fd, response, strlen(response));
                return;
            }

            //απενεργοποιούμε το watch με την βασική της inotify
            inotify_rm_watch(inotify_fd, curr->watch_descriptor);
            curr->active = 0;

            snprintf(response, sizeof(response), "%s Monitoring stopped for %s\n", timebuf, src_dir);
            write(pipe_out_fd, response, strlen(response));
            //γράφω στο αρχείο και στην οθόνη
            snprintf(log_entry, sizeof(log_entry), "%s Monitoring stopped for %s\n", timebuf, src_dir);
            write(log_fd, log_entry, strlen(log_entry));
            remove_from_pending_queue(src_dir); //κλήση για αφαίρεση εργασίας από την ουρά
            return;
        }
        curr = curr->next;  //επόμενος κόμβος
    }

    //αν δεν βρέθηκε καθόλου
    snprintf(response, sizeof(response), "%s Directory not monitored: %s\n", timebuf, src_dir);
    write(pipe_out_fd, response, strlen(response));
}

//βοηθητική συνάρτηση αναζήτησης στην βάση δεδομένων με κριτήριο το watch descriptor
sync_info_mem_store* find_entry_by_watch(int wd) {
    sync_info_mem_store *curr = sync_list_head; //η κορυφή της λίστας
    while (curr) { //μέχρι να τελειώσει ψάχνω τον wd
        if (curr->watch_descriptor == wd) return curr;
        curr = curr->next; //επόμενος κόμβος
    }
    return NULL;
}

//συνάρτηση διαχείρισης αλλαγών μέσω inotify
void handle_inotify_events(int inotify_fd, int log_fd) {
    char buffer[EVENT_BUF_LEN];
    int length = read(inotify_fd, buffer, EVENT_BUF_LEN); //για event
    if (length < 0) return; //σε περίπτωση που δεν έχω events

    int i = 0;
    while (i < length) {   //με το offset &buffer[i] περπατάω στο buffer
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        if (event->mask & (IN_CREATE | IN_MODIFY | IN_DELETE)) {
            sync_info_mem_store *entry = find_entry_by_watch(event->wd);
            if (entry && event->len > 0) { //αφού εντοπίσω το wd
                Operation op;
                if (event->mask & IN_CREATE) { //χρησιμοποιώ άλλο πεδίο της inotify_event
                    op = OP_ADDED;
                } else if (event->mask & IN_MODIFY) {
                    op = OP_MODIFIED;
                } else if (event->mask & IN_DELETE) {
                    op = OP_DELETED;
                }
                //καλώ την συνάρτηση για συγχρονισμό όταν εντοπίσει την αλλαγή
                sync_on_change(entry->source_dir, entry->target_dir, event->name, op, log_fd);
            }
        }
        //το βήμα=σταθερό μέγεθος + το len του name[]
        i += sizeof(struct inotify_event) + event->len; 
    }
}

//συνάρτηση συγχρονισμού για τις αλλαγές 
int sync_on_change(const char *src, const char *dst, const char *filename, Operation op, int log_fd){
    char msg[512];
    time_t now = time(NULL); //χρήση τρέχουσας ώρας
    char timebuf[64];       //formation εκφώνησης
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));

    sync_info_mem_store *entry = sync_list_head; //η κεφαλή της λίστας
    while (entry) {     //όσο έχει κόμβους
        if (strcmp(entry->source_dir, src) == 0 && strcmp(entry->target_dir, dst) == 0) {
            if (entry->is_syncing) { //μόλις βρει και τα 2 + syncing
                snprintf(msg, sizeof(msg), "%s Sync already in progress %s\n", timebuf, src);
                write(log_fd, msg, strlen(msg));
                write(STDOUT_FILENO, msg, strlen(msg));
                return 0; //δεν ξεκινά νέο worker
            }
            //αλλιώς ξεκινά νέο worker με την start_worker
            pid_t pid;
            int err_count = 0;
            int result = start_worker(src, dst, filename, op, &pid, &err_count, log_fd);
            if (result > 0) {
                entry->is_syncing = 0; //0 ώστε να ξέρω πως δεν συγχρονίζεται
                entry->running_worker_pid = pid;
                return 0;
            } else {
                //αλλιώς εμφάνισε λάθος 
                snprintf(msg, sizeof(msg), "%s Failed to start worker for: %s -> %s\n", timebuf, src, dst);
                write(log_fd, msg, strlen(msg));
                write(STDERR_FILENO, msg, strlen(msg));
                return -1;
            }
        }
        entry = entry->next; //πάμε στον επόμενο κόμβο
    }

    //αν δεν βρέθηκε το entry: λάθος
    snprintf(msg, sizeof(msg), "%s No sync entry found for %s -> %s\n", timebuf, src, dst);
    write(log_fd, msg, strlen(msg));
    write(STDERR_FILENO, msg, strlen(msg));
    return -1;
}

//βοηθητική συνάρτηση αναζήτησης στην βάση δεδομένων με κριτήριο το source_dir
sync_info_mem_store* find_entry_by_source_dir(const char *src) {
    sync_info_mem_store *curr = sync_list_head; //η κεφαλή της λίστας
    while (curr) {  //όσο έχει στοιχεία ψάχνει το src
        if (strcmp(curr->source_dir, src) == 0) return curr;
        curr = curr->next;  //πάει στον επόμενο κόμβο
    }
    return NULL;
}

//συνάρτηση εκκίνησης worker με fork και exec
int start_worker(const char *src, const char *dst, const char *filename, Operation op, pid_t *pid, int *errors, int log_fd) {
    if (active_worker_count >= worker_limit) {
        //ελέγχω μην ξεπεράσουν το όριο
        int next_end = (queue_end + 1) % MAX_TASK_QUEUE; //κυκλική ουρά
        if (next_end == queue_start) {
            fprintf(stderr, "Task queue overflow, dropping task\n");
            return -1;
        }

        //πρόσθεσε στην ουρά τα στοιχεία
        strncpy(task_queue[queue_end].src, src, sizeof(task_queue[queue_end].src));
        strncpy(task_queue[queue_end].dst, dst, sizeof(task_queue[queue_end].dst));
        strncpy(task_queue[queue_end].filename, filename, sizeof(task_queue[queue_end].filename));
        task_queue[queue_end].op = op;
        queue_end = next_end;
        return 0;
    }
    //φτιάχνω pipe 
    int to_worker[2], from_worker[2];
    if (pipe(to_worker) == -1 || pipe(from_worker) == -1) {
        perror("pipe");
        return -1;
    }
    //καλώ fork σε δείκτη
    *pid = fork();
    if ( *pid == -1) {
        perror("fork");
        return -1;
    }
    //όταν είμαι στο παιδί-worker
    if ( *pid == 0) {
        close(to_worker[1]); //κλείνω write_end
        close(from_worker[0]); //κλείνω read_end
    
        //ανακατευθύνω το stdin στο to_worker[0]
        if (dup2(to_worker[0], STDIN_FILENO) == -1) {
            perror("dup2(STDIN) failed");
            exit(1);
        }
        //αντί να εκτυπώνω στο stdout, θα εκτυπώνω στο from_worker[1]
        if (dup2(from_worker[1], STDOUT_FILENO) == -1) {
            perror("dup2(STDOUT) failed");
            exit(1);
        }
        //το ίδιο με πάνω αλλά με το stderr
        if (dup2(from_worker[1], STDERR_FILENO) == -1) {
            perror("dup2(STDERR) failed");
            exit(1);
        }
        
        close(to_worker[0]); //κλείνω ανάγνωση
        close(from_worker[1]);  //κλείνω γραφή

        char op_str[16];
        switch (op) {  //μετατρέπω το enum σε string
            case OP_FULL: strcpy(op_str, "FULL"); break;
            case OP_ADDED: strcpy(op_str, "ADDED"); break;
            case OP_MODIFIED: strcpy(op_str, "MODIFIED"); break;
            case OP_DELETED: strcpy(op_str, "DELETED"); break;
        }
        //χρήση exec για διαχωρισμό worker από manager με τα νέα ορίσματα
        execl("./worker", "./worker", src, dst, filename, op_str, NULL);
        const char *error_msg = "EXEC_FAILED\n"; //εδώ δεν θα πρέπει να φτάσει
        write(STDOUT_FILENO, error_msg, strlen(error_msg));
        perror("exec");
        exit(1);
    } else {
        //εδώ είμαστε στον manager
        close(to_worker[0]); //κλείνει ανάγνωση
        close(from_worker[1]);  //κλείνει εγγραφή

        int flags = fcntl(from_worker[0], F_GETFL, 0); //διαάζω τις ήδη υπάρχουσες σημαίες
        fcntl(from_worker[0], F_SETFL, flags | O_NONBLOCK);  //κάνω το pipe μη μπλοκαριστικό
        
        char buffer[1024] = {0};
        char temp[256];
        int total_read = 0;
        int found_end = 0;
        
        usleep(100000);  //μικρή καθυστέρηση για να ξεκινήσει ο worker
        
        while (1) {
            int n = read(from_worker[0], temp, sizeof(temp) - 1);
            if (n <= 0) break;  // Δεν υπάρχει άλλο διαθέσιμο, pipe είναι non-blocking
        
            temp[n] = '\0'; //βάζουμε μόνοι μας το null terminator
            if (total_read + n < sizeof(buffer) - 1) {
                strcat(buffer, temp);  //συννένωση συμβολοσειρών
                total_read += n;
            }
                //αν η συμβολοσειρά υπάρχει μέσα στο buffer
            if (strstr(buffer, "EXEC_REPORT_END") != NULL) {
                found_end = 1;
                break;
            }
        }
            //αν η συμβολοσειρά υπάρχει μέσα στο buffer
        if (strstr(buffer, "EXEC_FAILED") != NULL) {
            //κλείνω τα πάντα λόγω σφάλματος
            close(to_worker[1]);
            close(from_worker[0]);
            waitpid(*pid, NULL, 0); //περιμένω τον worker να τερματίσει
            return -1;
        }
        
        int local_errors = 0;

        if (found_end) {
    
            char *err_line = strstr(buffer, "ERRORS:");
            if (err_line) { //αν δεν υπάρχει τέτοια γραμμή λάθος
                sscanf(err_line, "ERRORS:%d", &local_errors);
        
            }
            char *status = NULL, *details = NULL;
            char *line = strtok(buffer, "\n"); //χωρίζω το buffer σε γραμμές

            while (line) {   //τα πρώτα 7 chr
                if (strncmp(line, "STATUS:", 7) == 0) {
                    status = line + 7;
                } else if (strncmp(line, "DETAILS:", 7) == 0) {
                    details = line + 8;
                }
                line = strtok(NULL, "\n");
            }

            if (status) while (*status == ' ') status++; //προσπερνάει το κενό
            if (details) while (*details == ' ') details++;

           //if (status && details) {
            time_t now = time(NULL); //χρήση τρέχουσας ώρας 
            struct tm *timeinfo = localtime(&now);
            char timebuf[64];   //formation
            strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", timeinfo);
            char log_message[1024];
            snprintf(log_message, sizeof(log_message),
                "%s [%s] [%s] [%d] [%s] [%s] [%s]\n",  //για να εμφανιστεί το μήνυμα όπως το θέλουμε
            timebuf,
            src,
            dst,
            *pid, 
            operation_to_string(op),  //enum->string
            status,
            details);

            write(log_fd, log_message, strlen(log_message));

        }

        if (errors) *errors = local_errors;
        //ρίσκω την entry βάσει src
        sync_info_mem_store *info = find_entry_by_source_dir(src);
        if (info != NULL) {
            info->is_syncing = 0; //αφού τελειώσει με συγχρονισμό το βάζω 0
        }        
        //αυξάνω τον πίνακα στην δομή
        ActiveWorker *w = &active_workers[active_worker_count++];
        w->pid = *pid;
        w->pipe_write = to_worker[1];
        w->pipe_read = from_worker[0];
        strncpy(w->src, src, sizeof(w->src)); //αντιγράφω τα στοιχεία
        strncpy(w->dst, dst, sizeof(w->dst));
        strncpy(w->filename, filename, sizeof(w->filename));
        w->op = op;

        return 1;
    }
}

//συνάρτηση μετατροπής enum σε string
const char* operation_to_string(Operation op) {
    switch (op) {
        case OP_FULL: return "FULL";
        case OP_ADDED: return "ADDED";
        case OP_MODIFIED: return "MODIFIED";
        case OP_DELETED: return "DELETED";
        default: return "UNKNOWN";
    }
}

//συνάρτηση απομάκρυνσης του worker που τελείωσε
void remove_worker_by_pid(pid_t pid, int log_fd) {
    for (int i = 0; i < active_worker_count; i++) {
        if (active_workers[i].pid == pid) { //όταν βρίσκει το pid τα κλείνει ολα
            close(active_workers[i].pipe_read);
            close(active_workers[i].pipe_write);

            //τους μειώνει από τον πίνακα της δομής
            active_workers[i] = active_workers[--active_worker_count];

            //αν υπάρχουν ακόμη εργασίες εκτέλεσε worker
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

//συνάρτηση απομάκρυνσης task (αγνόηση) από την ουρά εργασιών
void remove_from_pending_queue(const char *src_dir) {
    int count = (queue_end - queue_start + MAX_TASK_QUEUE) % MAX_TASK_QUEUE;
    int removed = 0;   //εκκρεμή task για να αποφύγω το αρνητικό πρόσιμο σε κυκλική ουρά

    int new_end = queue_start;

    for (int i = 0; i < count; i++) {
        int index = (queue_start + i) % MAX_TASK_QUEUE;

        if (!removed && strcmp(task_queue[index].src, src_dir) == 0) {
            //αν βρω αυτό που θέλω το προσπερνάω
            removed = 1;
            continue;
        }
            //προσπέραση
        task_queue[new_end] = task_queue[index];
        new_end = (new_end + 1) % MAX_TASK_QUEUE;
    }

    queue_end = new_end;

    if (removed) {      //σχολιασμένα για να αποφύγω το penalty
        //printf("Removed one pending task for %s\n", src_dir);
    } else {
        //printf("No pending task for %s\n", src_dir);
    }
}




//---------------------------FSS_CONSOLE-------------------------------------------
//συνάρτηση για την χρονοσφραγίδα εκφώνησης
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);  //χρήση τρέχουσας ώρας 
    struct tm *tm_info = localtime(&now); //χρήση δομής χρόνου + formation
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", tm_info);
}



//-----------------------------WORKER---------------------------------------------
//συνάρτηση μετατροπής string σε enum
Operation parse_operation(const char *op_str) {
    if (strcmp(op_str, "FULL") == 0) return OP_FULL;
    if (strcmp(op_str, "ADDED") == 0) return OP_ADDED;
    if (strcmp(op_str, "MODIFIED") == 0) return OP_MODIFIED;
    if (strcmp(op_str, "DELETED") == 0) return OP_DELETED;
    //σε περίπτωση λάθους
    fprintf(stderr, "Invalid operation: %s\n", op_str);
    exit(EXIT_FAILURE);
}

//συνάρτηση πλήρους συγχρονισμού-αντιγραφή όλων
void do_full_sync(const char *src, const char *dst, 
    int *files_copied, int *files_skipped, 
    char *error_buffer, size_t *error_offset) {
        //ανοίγω τον src φάκελο
    DIR *src_dir = opendir(src);
    if (!src_dir) {
        //σε περίπτωση λάθους καλώ την συνάρτηση σφάλματος
        log_error(src, strerror(errno), error_buffer, error_offset);
        return;
    }

    struct dirent *entry; //η δομή φακέλου
    char src_path[512], dst_path[512];
    //αν μπορώ να διαβάσω
    while ((entry = readdir(src_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;   //τα αγνοώ ως ειδικές καταχωρήσεις

        //δημιουργώ το πλήρες μονοπάτι   
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
        
        int success = 1;
        //άνοιγμα πλήρους path-αρχείου
        int src_fd = open(src_path, O_RDONLY);
        if (src_fd < 0) {  //περίπτωση λάθους
            log_error(src_path, strerror(errno), error_buffer, error_offset);
            (*files_skipped)++;
            continue;
        }
        //το ανοίγω για γράψιμο καθάρισμα με δικαιώματα rw -r -r
        int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dst_fd < 0) {  //περίπτωση λάθους
            log_error(dst_path, strerror(errno), error_buffer, error_offset);
            close(src_fd);
            (*files_skipped)++;
            continue;
        }

        char buffer[BUF_SIZE];
        ssize_t bytes;   //διαβάζω και γράφω
        while ((bytes = read(src_fd, buffer, BUF_SIZE)) > 0) {
            if (write(dst_fd, buffer, bytes) != bytes) {
                log_error(dst_path, "write error", error_buffer, error_offset);
                success = 0;
                break;
            }
        }
        //αν δεν διάβασα τίποτα
        if (bytes < 0) {
            log_error(src_path, "read error", error_buffer, error_offset);
            success = 0;
        }
        //κλείνω τους σχετικούς file descriptors
        close(src_fd);
        close(dst_fd);

        if (success) //αν μπόρεσα έως εδώ να διαβάσω
             (*files_copied)++;
        else
        //αλλιώς αυξάνω τα σκιπαρισμένα
             (*files_skipped)++;
    }
    //κλείνω τον φάκελο
    closedir(src_dir);
}

//συνάρτηση προσθήκης
void handle_added(const char *src, const char *dst, const char *filename, 
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset) {

    char src_path[512], dst_path[512]; //δημιουργώ το πλήρες μονοπάτι
    snprintf(src_path, sizeof(src_path), "%s/%s", src, filename);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, filename);
    //ανοίγω τον φάκελο
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {  //περίπτωση λάθους
        log_error(src_path, strerror(errno), error_buffer, error_offset);
        (*files_skipped)++;
        return;
    }
    //το ανοίγω για γράψιμο καθάρισμα με δικαιώματα rw -r -r
    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {  //περίπτωση λάθους
        log_error(dst_path, strerror(errno), error_buffer, error_offset);
        close(src_fd);
        (*files_skipped)++;
        return;
    }

    char buffer[BUF_SIZE];
    ssize_t bytes;
    int success = 1;  //διαβάζω τα bytes
    while ((bytes = read(src_fd, buffer, BUF_SIZE)) > 0) {
        if (write(dst_fd, buffer, bytes) != bytes) {
            //αλλιώς όχι επιτυχία
            log_error(dst_path, "write error", error_buffer, error_offset);
            success = 0;
            break;
        }
    }
    //άλλο σφάλμα εδώ
    if (bytes < 0) {
        log_error(src_path, "read error", error_buffer, error_offset);
        success = 0;
    }
    //κλείνω τα fd
    close(src_fd);
    close(dst_fd);

    //επιτυχία και αποτυχία αντίστοιχα
    if (success)
        (*files_copied)++;
    else
        (*files_skipped)++;
}

//συνάρτηση τροποποίησης (σαν την προσθήκη)
void handle_modified(const char *src_dir, const char *dst_dir, const char *filename,
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset) {
    //απλώς αντικαθιστά το ήδη υπάρχον αρχείο
    handle_added(src_dir, dst_dir, filename, files_copied, files_skipped, error_buffer, error_offset);
}

//συνάρτηση διαγραφής
void handle_deleted(const char *dst, const char *filename,
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset) {

    char dst_path[512];
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, filename);
        //ξεσυνδέω το αρχείο
    if (unlink(dst_path) < 0) { //περίπτωση λάθους
        log_error(dst_path, strerror(errno), error_buffer, error_offset);
        (*files_skipped)++;
    } else {
        (*files_copied)++;  
    }
}

//συνάρτηση αποστολής της αναφοράς στον manager 
void send_exec_report_to_buffer(char *dest_buffer, size_t buffer_size, const char *status, int copied, int skipped, const char *error_buffer) {
    char temp[256];
    snprintf(dest_buffer, buffer_size, "EXEC_REPORT_START\n");
    //η αναφορά όπως είναι γραμμένη στην εκφώνηση
    snprintf(temp, sizeof(temp), "STATUS: %s\n", status);
    strncat(dest_buffer, temp, buffer_size - strlen(dest_buffer) - 1);
    //συννένωση μέχρι τον μέγιστο αριθμό 
    snprintf(temp, sizeof(temp), "DETAILS: %d files copied, %d skipped\n", copied, skipped);
    strncat(dest_buffer, temp, buffer_size - strlen(dest_buffer) - 1);

    if (strlen(error_buffer) > 0) {
        strncat(dest_buffer, "ERRORS:\n", buffer_size - strlen(dest_buffer) - 1);
        strncat(dest_buffer, error_buffer, buffer_size - strlen(dest_buffer) - 1);
    }

    strncat(dest_buffer, "EXEC_REPORT_END\n", buffer_size - strlen(dest_buffer) - 1);
}

//συνάρτηση που εκτυπώνει τα errors
void log_error(const char *path, const char *msg, char *buffer, size_t *offset) {
    int written = snprintf(buffer + *offset, BUF_SIZE - *offset,
                           "[%s] %s\n", path, msg); //γράφω χωρίς υπερχείλιση
    if (written > 0) {
        *offset += written;
        if (*offset >= BUF_SIZE)
            *offset = BUF_SIZE - 1;  // για ασφάλεια
    }
}
