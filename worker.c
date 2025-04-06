#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define BUF_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 5) {
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
            do_full_sync(src_dir, dst_dir);
            break;

        case OP_ADDED:
            handle_added(src_dir, dst_dir, filename);
            break;

        case OP_MODIFIED:
            handle_modified(src_dir, dst_dir, filename);
            break;

        case OP_DELETED:
            handle_deleted(dst_dir, filename);
            break;
    }

    
    return 0;
}
