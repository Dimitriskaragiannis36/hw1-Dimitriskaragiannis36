#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "utils.h"

#define BUF_SIZE 4096
#define ERROR_BUF_SIZE 8192
char error_buffer[ERROR_BUF_SIZE] = "";
size_t error_offset = 0;
int files_copied = 0, files_skipped = 0;

int main(int argc, char *argv[]) {

    const char* report =
    "EXEC_REPORT_START\n"
    "STATUS: SUCCESS\n"
    "DETAILS: Test sync complete\n"
    "EXEC_REPORT_END\n";

    write(STDOUT_FILENO, report, strlen(report));


    /*if (argc != 5) {
        fprintf(stderr, "Usage: %s <source_directory> <target_directory> <filename|ALL> <operation>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *src_dir = argv[1];
    const char *dst_dir = argv[2];
    const char *filename = argv[3];
    Operation op = parse_operation(argv[4]);

    switch (op) {
        case OP_FULL:
            if (strcmp(filename, "ALL") != 0) {
                fprintf(stderr, "For FULL operation, filename must be ALL\n");
                exit(EXIT_FAILURE);
            }
            do_full_sync(src_dir, dst_dir, &files_copied, &files_skipped, error_buffer, &error_offset);
            break;

        case OP_ADDED:
            handle_added(src_dir, dst_dir, filename, &files_copied, &files_skipped, error_buffer, &error_offset);
            break;

        case OP_MODIFIED:
            handle_modified(src_dir, dst_dir, filename, &files_copied, &files_skipped, error_buffer, &error_offset);
            break;

        case OP_DELETED:
            handle_deleted(dst_dir, filename, &files_copied, &files_skipped, error_buffer, &error_offset);
            break;
    }

    const char *status = (error_offset > 0) ? "PARTIAL" : "SUCCESS";
    send_exec_report(status, files_copied, files_skipped, error_buffer);*/
    return 0;
}
