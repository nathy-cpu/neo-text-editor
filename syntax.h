#ifndef SYNTAX_H
#define SYNTAX_H

char* CfileExtensions[] = {".c", ".h", NULL};

char* CppfileExtensions[] = {".cpp", ".hpp",NULL};

char* Ckeywords[] = {
    "alignas",  "alignof", "auto", "break", "case", "char", "const", "constexpr", "continue", "default",
    "do", "double", "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline",
    "nullptr", "register", "restrict", "return", "sizeof", "static", "static_assert",
    "struct", "switch", "thread_local", "true", "typedef", "typeof", "typeof_unqual", "union", "void",
    "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_BitInt", "_Bool", "_Complex", "_Decimal128",
    "_Decimal32", "_Decimal64", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
    "_Alignas", "_Alignof", "_Atomic", "_BitInt", "_Bool", "_Complex", "_Decimal128", "_Decimal32", "_Decimal64",
    "_Generiu", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local", "size_t", "ssize_t", NULL
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
    "throw",  "true", "try",  "typedef",  "typeid",  "typename",  "union",  "unsigned",  "using",  "virtual",  "void",
    "volatile", "wchar_t",  "while",  "xor",  "xor_eq ",  NULL
};

char* Cpptypes[] = {
    "bool", "char", "char8_t",  "char16_t", "char32_t", "float", "double", "int",  "long", "short",  "signed", NULL
};


Syntax HLDB[] =
{
    {
        "C",
        CfileExtensions,
        Ckeywords,
        Ctypes,
        "//", "/*", "*/",
        HIGHLIGHT_ALL
    },
    {
        "C++",
        CppfileExtensions,
        Cppkeywords,
        Cpptypes,
        "//", "/*", "*/",
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
        0
    }
};

#endif // SYNTAX_H
