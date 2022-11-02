#include <stdio.h>
#include <string.h>

void usage() {
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0> ...\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return -1;
    }

    char* op = argv[1];
    if (strcmp(op, "write") == 0) {
        /* 
         * Please encode the input file with EVENODD code
         * and store the erasure-coded splits into corresponding disks
         * For example: Suppose "file_name" is "testfile", and "p" is 5. After your
         * encoding logic, there should be 7 splits, "testfile_0", "testfile_1",
         * ..., "testfile_6", stored in 7 diffrent disk folders from "disk_0" to
         * "disk_6".
         */
    
    } else if (strcmp(op, "read")) {
        /* 
         * Please read the file specified by "file_name", and store it as a file
         * named "save_as" in the local file system.
         * For example: Suppose "file_name" is "testfile" (which we have encoded 
         * before), and "save_as" is "tmp_file". After the read operation, there 
         * should be a file named "tmp_file", which is the same as "testfile".
         */
    } else if (strcmp(op, "repair")) {
        /*
         * Please repair failed disks. The number of failures is specified by
         * "num_erasures", and the index of disks are provided in the command
         * line parameters.
         * For example: Suppose "number_erasures" is 2, and the indices of
         * failed disks are "0" and "1". After the repair operation, the data
         * splits in folder "disk_0" and "disk_1" should be repaired.
         */
    } else {
        printf("Non-supported operations!\n");
    }
    return 0;
}

