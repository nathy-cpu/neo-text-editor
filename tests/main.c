#include <stdio.h>
#include "test_file_io.c"
#include "test_terminal.c"
#include "test_gap_buffer.c"
#include "test_array.c"
#include "test_slice.c"
#include "test_line.c"
#include "test_buffer.c"
#include "test_config.c"
#include "test_args.c"

int main(void) {
    // Call terminal tests
    printf("Running terminal tests...\n\r");
    test_Terminal_EnableRawMode();
    test_Terminal_DisableRawMode();
    test_Terminal_Restore();
    test_Terminal_ClearScreen();
    printf("All terminal tests passed!\n\r");
    
    // Call CLI args tests
    printf("Running CLI args tests...\n\r");
    test_args_basic();
    test_args_enablers();
    test_args_jumps_forward();
    test_args_jumps_backward();
    test_args_help_version();
    printf("All CLI args tests passed!\n\r");
    
    // Call gap buffer tests
    printf("Running gap buffer tests...\n\r");
    test_gap_buffer_basic();
    printf("All gap buffer tests passed!\n\r");

    printf("Running array tests...\n\r");
    test_array_basic();
    test_array_growth();
    printf("All array tests passed!\n\r");

    printf("Running slice tests...\n\r");
    test_slice_basic();
    printf("All slice tests passed!\n\r");

    printf("Running line tests...\n\r");
    test_line_basic();
    test_line_render_x();
    printf("All line tests passed!\n\r");

    printf("Running config tests...\n\r");
    test_config_defaults();
    test_config_lua_load();
    printf("All config tests passed!\n\r");

    printf("Running buffer tests...\n\r");
    test_buffer_basic();
    test_buffer_lines();
    test_tab_gutter();
    test_tab_wrapping();
    printf("All buffer tests passed!\n\r");

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
