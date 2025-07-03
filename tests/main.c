#include "file_io_test.c"

int main() {
    test_read_write();
    test_mmap();
    test_errors();
    test_null_safety();
    test_large_file();
    printf("All file_io tests passed!\n");
    return 0;
}