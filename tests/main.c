#include <stdio.h>
#include "file_io_test.c"
#include "terminal_test.c"

int main(void) {
    // Call terminal tests
    printf("Running terminal tests...\n\r");
    test_Terminal_EnableRawMode();
    test_Terminal_DisableRawMode();
    test_Terminal_Restore();
    test_Terminal_ClearScreen();
    printf("All terminal tests passed!\n\r");
    // Call other tests here as needed
    printf("Running file I/O tests...\n\r");
    test_read_write();
    test_mmap();
    test_errors();
    test_null_safety();
    test_large_file();
    printf("All file I/O tests passed!\n\r");
    return 0;
}