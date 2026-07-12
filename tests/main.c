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
#include "test_log.c"

int main(void) {
    // Call terminal tests
    printf("Running terminal tests...\n\r");
    LOG_INFO("Starting terminal tests...");
    test_Terminal_EnableRawMode();
    test_Terminal_DisableRawMode();
    test_Terminal_Restore();
    test_Terminal_ClearScreen();
    printf("All terminal tests passed!\n\r");
    LOG_INFO("All terminal tests passed!");
    
    // Call CLI args tests
    printf("Running CLI args tests...\n\r");
    LOG_INFO("Starting CLI args tests...");
    test_args_basic();
    test_args_enablers();
    test_args_jumps_forward();
    test_args_jumps_backward();
    test_args_help_version();
    printf("All CLI args tests passed!\n\r");
    LOG_INFO("All CLI args tests passed!");
    
    // Call gap buffer tests
    printf("Running gap buffer tests...\n\r");
    LOG_INFO("Starting gap buffer tests...");
    test_gap_buffer_basic();
    printf("All gap buffer tests passed!\n\r");
    LOG_INFO("All gap buffer tests passed!");

    printf("Running array tests...\n\r");
    LOG_INFO("Starting array tests...");
    test_array_basic();
    test_array_growth();
    test_array_replace_range_shrink();
    test_array_replace_range_grow();
    test_array_replace_range_ends();
    printf("All array tests passed!\n\r");
    LOG_INFO("All array tests passed!");

    printf("Running slice tests...\n\r");
    LOG_INFO("Starting slice tests...");
    test_slice_basic();
    printf("All slice tests passed!\n\r");
    LOG_INFO("All slice tests passed!");

    printf("Running line tests...\n\r");
    LOG_INFO("Starting line tests...");
    test_line_basic();
    test_line_render_x();
    printf("All line tests passed!\n\r");
    LOG_INFO("All line tests passed!");

    printf("Running config tests...\n\r");
    LOG_INFO("Starting config tests...");
    test_config_defaults();
    test_config_lua_load();
    printf("All config tests passed!\n\r");
    LOG_INFO("All config tests passed!");

    printf("Running buffer tests...\n\r");
    LOG_INFO("Starting buffer tests...");
    test_buffer_basic();
    test_buffer_lines();
    test_tab_gutter();
    test_tab_wrapping();
    test_tab_folding();
    test_editor_toggle_all_folds();
    test_buffer_piece_table();
    test_buffer_last_rebuild_range();
    test_tab_visual_rows_incremental();
    printf("All buffer tests passed!\n\r");
    LOG_INFO("All buffer tests passed!");

    // Call other tests here as needed
    printf("Running file I/O tests...\n\r");
    LOG_INFO("Starting file I/O tests...");
    test_read_write();
    test_mmap();
    test_errors();
    test_null_safety();
    test_large_file();
    printf("All file I/O tests passed!\n\r");
    LOG_INFO("All file I/O tests passed!");

    printf("Running logger tests...\n\r");
    LOG_INFO("Starting logger tests...");
    test_log_basic();
    test_log_file();
    test_log_reconfigure();
    printf("All logger tests passed!\n\r");
    LOG_INFO("All logger tests passed!");

    return 0;
}
