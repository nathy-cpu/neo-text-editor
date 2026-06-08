tab_size = 4
show_line_numbers = true
wrap_lines = false
syntax_enabled = true
status_timeout = 3

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
    save_as = "alt-s"
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
