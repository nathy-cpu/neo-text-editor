#ifndef SYNTAX_H
#define SYNTAX_H

char* CfileExtensions[] = {".c", ".h", NULL};

char* CppfileExtensions[] = {".cpp", ".hpp", ".cc", ".cxx",NULL};

char* JavafileExtensions[] = {".java", NULL};

char* C_CppPreprocessors[] = {
    "#if", "#ifdef", "#ifndef", "#else", "#elif", "#elifdef", "#elifndef", "#endif", "#define", "undef", "#include",
    "#error", "#warning", "#pragma", "#line", NULL
};

char* Ckeywords[] = {
    "alignas",  "alignof", "auto", "break", "case", "const", "constexpr", "continue", "default",
    "do", "double", "else", "enum", "extern", "false", "for", "goto", "if", "inline",
    "nullptr", "register", "restrict", "return", "sizeof", "static", "static_assert","struct",
    "switch", "thread_local", "true", "typedef", "typeof", "typeof_unqual", "union",
    "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_BitInt", "_Bool", "_Complex", "_Decimal128",
    "_Decimal32", "_Decimal64", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
    "_Alignas", "_Alignof", "_Atomic", "_BitInt", "_Bool", "_Complex", "_Decimal128", "_Decimal32", "_Decimal64",
    "_Generiu", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local", NULL
};

char* Ctypes[] = {
    "int", "long", "short", "double", "float", "char", "unsigned", "signed", "void", "bool", NULL
};

char* Cppkeywords[] = {
    "alignas",  "alignof",  "and",  "and_eq",  "asm",  "atomic_cancel",  "atomic_commit",  "atomic_noexcept", "auto",
    "bitand",  "bitor", "break",  "case",  "catch", "class",  "compl",  "concept",  "const",  "consteval",  "constexpr",
    "constinit",  "const_cast", "continue",  "co_await",  "co_return",  "co_yield",  "decltype",  "default",  "delete",
    "do", "dynamic_cast",  "else",  "enum",  "explicit",  "export",  "extern",  "false", "for",  "friend", "goto",  "if",
    "inline", "mutable",  "namespace",  "new",  "noexcept",  "not",  "not_eq", "nullptr",  "operator",  "or",  "or_eq",
    "private",  "protected",  "public",  "reflexpr",  "register", "reinterpret_cast",  "requires",  "return", "sizeof",
    "static",  "static_assert", "static_cast",  "struct",  "switch",  "synchronized",  "template",  "this",  "thread_local",
    "throw",  "true", "try",  "typedef",  "typeid",  "typename",  "union",  "using",  "virtual",
    "volatile",  "while",  "xor",  "xor_eq ",  NULL
};

char* Cpptypes[] = {
    "bool", "char", "wchar_t", "char8_t", "char16_t", "char32_t", "float", "unsigned", "double", "int", "void", "long", "short", "signed", NULL
};

char* Javakeywords[] = {
    "abstract", "continue", "for", "new", "switch", "assert", "default", "goto", "package", "synchronized", "do",
    "if", "private", "this", "break", "implements", "protected", "throw", "else", "import", "public", "true",
    "throws", "case", "enum", "instanceof", "return", "transient", "catch", "extends", "try", "final", "false", "null",
    "interface", "static", "class", "finally", "strictfp", "volatile", "const", "native", "super", "while", NULL
};

char* Javatypes[] = {
    "boolean", "double", "byte", "int", "short", "char", "void", "long", "float", NULL
};

Syntax HLDB[] =
{
    {
        "C",
        CfileExtensions,
        Ckeywords,
        Ctypes,
        "//", "/*", "*/",
        C_CppPreprocessors,
        HIGHLIGHT_ALL
    },
    {
        "C++",
        CppfileExtensions,
        Cppkeywords,
        Cpptypes,
        "//", "/*", "*/",
        C_CppPreprocessors,
        HIGHLIGHT_ALL
    },
    {
        "Java",
        JavafileExtensions,
        Javakeywords,
        Javatypes,
        "//", "/*", "*/",
        NULL,
        HIGHLIGHT_ALL
    },
    {
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0
    }
};

#endif // SYNTAX_H
