#ifndef UTILS_H
#define UTILS_H

#include <time.h>     //για την last_sync_time
#include <sys/inotify.h>   //για struct inotify_event

#define PIPE_IN "fss_in"
#define PIPE_OUT "fss_out"

//για έως 16χαρ filenames με 1024 events
#define EVENT_BUF_LEN (1024 * (sizeof(struct inotify_event) + 16))
#define MAX_WORKERS 50
#define MAX_TASK_QUEUE 100

//η δομή της εκφώνισης
typedef struct sync_info_mem_store {
    char source_dir[256];
    char target_dir[256];
    int active;
    int error_count;
    int is_syncing; 
    int watch_descriptor; 
    pid_t running_worker_pid;
    time_t last_sync_time;
    struct sync_info_mem_store *next;
} sync_info_mem_store;

//λειτουργίες συγχρονισμού
typedef enum {
    OP_FULL,
    OP_ADDED,
    OP_MODIFIED,
    OP_DELETED
} Operation;

//η βοηθητική δομή για ενεργούς workers
typedef struct {
    pid_t pid;
    int pipe_write;
    int pipe_read;
    char src[256];
    char dst[256];
    char filename[256];
    Operation op;
} ActiveWorker;

//η δομή για ενεργά tasks
typedef struct {
    char src[256];
    char dst[256];
    char filename[256];
    Operation op;
} WorkerTask;

//ορίζονται στο utils.c
extern int worker_limit;
extern sync_info_mem_store *sync_list_head;


//--------------------------FSS_MANAGER--------------------------------------------
//συνάρτηση καθαρισμού pipes και logfile
void cleanup_previous_state(const char *logfile);

//συνάρτηση φόρτωσης ζευγών από το config_file
void load_config_file(const char *config_path, int inotify_fd, int log_fd, int fd_out);

//συνάρτηση χειρισμού command μεταξύ manager με console
int handle_command(const char *cmd, int pipe_out_fd, int pipe_in_fd, int log_fd, int inotify_fd);

//συνάρτηση παρακολούθησης καταλόγου με inotify
int add_watch_entry(int inotify_fd, const char *source, const char *target, int log_fd, int fd_out);

//συνάρτηση αρχικού συγχρονισμού (από τα ζεύγη του config_file)
int perform_initial_sync(const char *src, const char *dst, int log_fd);

//συνάρτηση διακοπής παρακολούθησης καταλόγου με inotify
void remove_watch_entry(const char *src_dir, int inotify_fd, int log_fd, int pipe_out_fd);

//συνάρτηση διαχείρισης αλλαγών μέσω inotify
void handle_inotify_events(int inotify_fd, int log_fd);

//συνάρτηση συγχρονισμού για τις αλλαγές 
int sync_on_change(const char *src, const char *dst, const char *filename, Operation op, int log_fd);

//συνάρτηση εκκίνησης worker με fork και exec
int start_worker(const char *src, const char *dst, const char *filename, Operation op, pid_t *pid, int *errors, int log_fd);

//συνάρτηση μετατροπής enum σε string
const char* operation_to_string(Operation op);

//συνάρτηση απομάκρυνσης του worker που τελείωσε
void remove_worker_by_pid(pid_t pid, int log_fd);

//συνάρτηση απομάκρυνσης task (αγνόηση) από την ουρά εργασιών
void remove_from_pending_queue(const char *src_dir);



//---------------------------FSS_CONSOLE-------------------------------------------
//συνάρτηση για την χρονοσφραγίδα εκφώνησης
void get_timestamp(char *buffer, size_t size);



//-----------------------------WORKER---------------------------------------------
//συνάρτηση μετατροπής string σε enum
Operation parse_operation(const char *op_str);

//συνάρτηση πλήρους συγχρονισμού-αντιγραφή όλων
void do_full_sync(const char *src, const char *dst, 
    int *files_copied, int *files_skipped, 
    char *error_buffer, size_t *error_offset);

//συνάρτηση προσθήκης 
void handle_added(const char *src_dir, const char *dst_dir, const char *filename,
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset);

//συνάρτηση τροποποίησης (σαν την προσθήκη)
void handle_modified(const char *src_dir, const char *dst_dir, const char *filename,
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset);

//συνάρτηση διαγραφής
void handle_deleted(const char *dst_dir, const char *filename,
    int *files_copied, int *files_skipped,
    char *error_buffer, size_t *error_offset);

//συνάρτηση αποστολής της αναφοράς στον manager 
void send_exec_report_to_buffer(char *dest_buffer, size_t buffer_size, const char *status, int copied, int skipped, const char *error_buffer);

//συνάρτηση που εκτυπώνει τα errors
void log_error(const char *path, const char *msg, char *buffer, size_t *offset);


#endif //UTILS_H
