#include <stdio.h>
#include <stdlib.h>    //για exit
#include <string.h>   //strcmp
#include <unistd.h>     //για low I/0 
#include "utils.h"      //βιβλιοθήκη με όλες τις απαραίτητες συναρτήσεις


#define ERROR_BUF_SIZE 8192

char error_buffer[ERROR_BUF_SIZE] = "";  //αρχικοποίηση null
size_t error_offset = 0;
int files_copied = 0, files_skipped = 0;

//κυρίως συνάρτηση
int main(int argc, char *argv[]) {
    
    //έλεγχος ορισμάτων
    if (argc != 5) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Usage: %s <source_directory> <target_directory> <filename|ALL> <operation>\n",
                 argv[0]);
        write(STDOUT_FILENO, msg, strlen(msg));  
        exit(EXIT_FAILURE);
    }
    
    //ανάθεση ορισμάτων σε τοπικές μεταβλητές
    const char *src_dir = argv[1];
    const char *dst_dir = argv[2];
    const char *filename = argv[3];
    Operation op = parse_operation(argv[4]); //enum στο utils 

    switch (op) {
        case OP_FULL:
            if (strcmp(filename, "ALL") != 0) {
                const char *err_msg = "For FULL operation, filename must be ALL\n";
                write(STDERR_FILENO, err_msg, strlen(err_msg));
                exit(EXIT_FAILURE);
            }                           //συνάρτηση για full συγχρονισμό στο utils
            do_full_sync(src_dir, dst_dir, &files_copied, &files_skipped, error_buffer, &error_offset);
            break;

        case OP_ADDED:                  //συνάρτηση για προσθήκη-αντιγραφή στο utils
            handle_added(src_dir, dst_dir, filename, &files_copied, &files_skipped, error_buffer, &error_offset);
            break;

        case OP_MODIFIED:               //συνάρτηση για τροποποίηση στο utils
            handle_modified(src_dir, dst_dir, filename, &files_copied, &files_skipped, error_buffer, &error_offset);
            break;

        case OP_DELETED:                //συνάρτηση για διαγραφή στο utils
            handle_deleted(dst_dir, filename, &files_copied, &files_skipped, error_buffer, &error_offset);
            break;
    }

    //string για την προσθήκη PARTIAL ή SUCCESS
    const char *status = (error_offset > 0) ? "PARTIAL" : "SUCCESS";
    char report_buffer[4096];           //συνάρτηση αναφοράς στο utils
    send_exec_report_to_buffer(report_buffer, sizeof(report_buffer), status, files_copied, files_skipped, error_buffer);
    //επιστρέφει το buffer στον manager
    write(STDOUT_FILENO, report_buffer, strlen(report_buffer));
    exit(0);  
}
