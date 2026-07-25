tab_size = 4
show_line_numbers = true
wrap_lines = false
fsync_enabled = true
syntax_enabled = true
status_timeout = 3

-- Logging Configuration
log_file = "neo.log"
log_level = "INFO"
log_to_file = true
log_to_ui = true
log_max_messages = 1000

colors = {
    keyword = "35",
    type = "36",
    string = "32",
    comment = "90"
}

keybindings = {
    save = "ctrl-s",
    quit = "ctrl-q",
    new_tab = "ctrl-t",
    close_tab = "ctrl-w",
    next_tab = "ctrl-n",
    prev_tab = "ctrl-p",
    save_as = "alt-s",
    show_logs = "ctrl-l",
    toggle_fold = "ctrl-f",
    toggle_all_folds = "alt-f",
    copy = "ctrl-c",
    cut = "ctrl-x",
    paste = "ctrl-v",
    delete_line = "ctrl-d",
    move_line_up = "alt-arrow_up",
    move_line_down = "alt-arrow_down",
    join_lines = "ctrl-j",
    kill_to_end = "ctrl-k"
}

languages = {
    {
        name = "Lua",
        extensions = { ".lua" },
        keywords = { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while" },
        types = { "io", "math", "string", "table", "os" },
        single_line_comment = "--",
        multi_line_comment_start = "--[[",
        multi_line_comment_end = "]]"
    }
}
