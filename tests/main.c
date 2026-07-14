// Must come before any system header (even <stdio.h> below): glibc's
// <features.h> computes its internal __USE_* visibility macros the first time
// it's pulled in and then guards against reprocessing, so defining these
// afterwards -- e.g. only inside neo.h, included transitively by the very
// next line -- would be too late to unlock POSIX/GNU declarations (strdup,
// setenv, unsetenv, ...) for the rest of this translation unit.
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
#include "test_history.c"
#include "test_syntax.c"
#include "test_input.c"

int main(void) {
    // Call terminal tests
    printf("Running terminal tests...\n\r");
    LOG_INFO("Starting terminal tests...");
    test_Terminal_EnableRawMode();
    test_Terminal_EnableRawMode_idempotent();
    test_Terminal_DisableRawMode();
    test_Terminal_DisableRawMode_never_enabled();
    test_Terminal_Restore();
    test_Terminal_Restore_never_enabled();
    test_Terminal_ClearScreen();
    test_ReadKey_plain_char();
    test_ReadKey_arrows();
    test_ReadKey_home_end_variants();
    test_ReadKey_delete_and_page_keys();
    test_ReadKey_ctrl_and_shift_arrows();
    test_ReadKey_alt_s_and_alt_f();
    test_ReadKey_lone_escape_returns_escape();
    test_Terminal_GetCursorPosition();
    test_Terminal_GetWindowSize();
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
    test_args_missing_value_errors();
    test_args_invalid_value_errors();
    test_args_unknown_option();
    test_args_log_overrides();
    test_args_no_args();
    test_args_bare_plus_is_a_filename();
    test_args_trailing_jump_with_no_file_is_dropped();
    test_args_free_resets_state();
    printf("All CLI args tests passed!\n\r");
    LOG_INFO("All CLI args tests passed!");
    
    // Call gap buffer tests
    printf("Running gap buffer tests...\n\r");
    LOG_INFO("Starting gap buffer tests...");
    test_gap_buffer_basic();
    test_gap_buffer_capacity_growth();
    test_gap_buffer_move_gap_edges();
    test_gap_buffer_zero_size_noops();
    test_gap_buffer_clear();
    test_gap_buffer_to_slice_after_delete_mid_buffer();
    printf("All gap buffer tests passed!\n\r");
    LOG_INFO("All gap buffer tests passed!");

    printf("Running array tests...\n\r");
    LOG_INFO("Starting array tests...");
    test_array_basic();
    test_array_growth();
    test_array_replace_range_shrink();
    test_array_replace_range_grow();
    test_array_replace_range_ends();
    test_array_replace_range_grow_with_tail();
    test_array_replace_range_exact_capacity_fit();
    test_array_pop_empty();
    test_array_at_and_raw_at();
    test_array_free_resets_state();
    test_array_init_natural_alignment();
    printf("All array tests passed!\n\r");
    LOG_INFO("All array tests passed!");

    printf("Running slice tests...\n\r");
    LOG_INFO("Starting slice tests...");
    test_slice_basic();
    test_slice_make_and_zero_size();
    test_slice_subslice_bounds();
    printf("All slice tests passed!\n\r");
    LOG_INFO("All slice tests passed!");

    printf("Running line tests...\n\r");
    LOG_INFO("Starting line tests...");
    test_line_basic();
    test_line_get_char();
    test_line_insert_position_clamping();
    test_line_delete_edges();
    test_line_buffer_backed_delegation();
    test_line_render_x();
    test_line_render_x_edges();
    printf("All line tests passed!\n\r");
    LOG_INFO("All line tests passed!");

    printf("Running config tests...\n\r");
    LOG_INFO("Starting config tests...");
    test_config_defaults();
    test_config_defaults_full();
    test_config_lua_load();
    test_config_lua_load_full();
    test_config_load_missing_file_fails();
    test_config_load_malformed_lua_fails();
    test_config_load_language_replace_and_discard();
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
    test_buffer_undo_redo_insert_char();
    test_buffer_undo_redo_grouping_boundary();
    test_buffer_undo_redo_delete_split_join();
    test_buffer_undo_then_new_edit_clears_redo();
    test_buffer_redo_stack_reused_group_no_leak();
    test_buffer_undo_redo_empty_stacks();
    test_buffer_undo_respects_limit();
    printf("All buffer tests passed!\n\r");
    LOG_INFO("All buffer tests passed!");

    printf("Running history tests...\n\r");
    LOG_INFO("Starting history tests...");
    test_stack_basic();
    test_stack_enforce_limit();
    test_stack_free();
    test_action_group_lifecycle();
    test_history_init_free();
    printf("All history tests passed!\n\r");
    LOG_INFO("All history tests passed!");

    printf("Running syntax tests...\n\r");
    LOG_INFO("Starting syntax tests...");
    test_get_syntax_color_defaults();
    test_get_syntax_color_config_override();
    test_syntax_highlight_c_file();
    test_syntax_highlight_strings_and_chars();
    test_syntax_highlight_multiline_comment();
    test_syntax_no_match_leaves_syntax_null();
    printf("All syntax tests passed!\n\r");
    LOG_INFO("All syntax tests passed!");

    printf("Running input tests...\n\r");
    LOG_INFO("Starting input tests...");
    test_move_cursor_left_right_within_line();
    test_move_cursor_left_right_across_lines();
    test_move_cursor_up_down_clamps_column();
    test_move_cursor_word_right_left();
    test_get_selection_normalizes_direction();
    test_delete_selection_single_and_multi_line();
    test_toggle_fold_on_foldable_and_non_foldable_line();
    printf("All input tests passed!\n\r");
    LOG_INFO("All input tests passed!");

    // Call other tests here as needed
    printf("Running file I/O tests...\n\r");
    LOG_INFO("Starting file I/O tests...");
    test_read_write();
    test_mmap();
    test_errors();
    test_null_safety();
    test_mmap_threshold_boundary();
    test_zero_byte_file();
    test_write_failure();
    test_mmap_open_failure();
    test_mapped_file_unmap_safety();
    test_large_file();
    printf("All file I/O tests passed!\n\r");
    LOG_INFO("All file I/O tests passed!");

    printf("Running logger tests...\n\r");
    LOG_INFO("Starting logger tests...");
    test_log_basic();
    test_log_file();
    test_log_reconfigure();
    test_log_parse_level_and_level_to_string();
    test_log_double_init_is_noop();
    test_log_configure_from_uninitialized();
    test_log_file_path_change();
    test_log_message_truncation();
    test_log_uninitialized_get_messages_and_before_init();
    printf("All logger tests passed!\n\r");
    LOG_INFO("All logger tests passed!");

    return 0;
}
