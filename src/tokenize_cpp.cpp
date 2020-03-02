#include "tokenize_cpp.hpp"

#include <ctype.h>
#include "contents.hpp"
#include "token.hpp"

namespace mag {

static void skip_whitespace(const Contents* contents, uint64_t* point) {
    while (*point < contents->len() && isspace((*contents)[*point])) {
        ++*point;
    }
}

static bool matches(const Contents* contents, uint64_t point, uint64_t end, cz::Str query) {
    if (end - point != query.len) {
        return false;
    }
    if (point + query.len > contents->len()) {
        return false;
    }
    for (size_t i = 0; i < query.len; ++i) {
        if ((*contents)[point + i] != query[i]) {
            return false;
        }
    }
    return true;
}

bool cpp_next_token(const Contents* contents, uint64_t point, Token* token) {
    skip_whitespace(contents, &point);

    if (point == contents->len()) {
        return false;
    }

    char first_char = (*contents)[point];
    if (first_char == '"') {
        token->start = point;
        ++point;
        for (; point < contents->len(); ++point) {
            if ((*contents)[point] == '"') {
                ++point;
                break;
            }
            if ((*contents)[point] == '\\') {
                if (point + 1 < contents->len()) {
                    // only skip over next character if we don't go out of bounds
                    ++point;
                }
            }
        }
        token->end = point;
        token->type = Token_Type::STRING;
        return true;
    }

    if (first_char == '\'') {
        token->start = point;
        if (point + 3 >= contents->len()) {
            token->end = contents->len();
        } else if ((*contents)[point + 1] == '\\') {
            token->end = point + 4;
        } else {
            token->end = point + 3;
        }
        token->type = Token_Type::STRING;
        return true;
    }

    if (isalpha(first_char) || first_char == '_') {
        token->start = point;
        while (++point < contents->len() &&
               (isalnum((*contents)[point]) || (*contents)[point] == '_')) {
        }
        token->end = point;

        cz::Str keywords[] = {
            "alignas",
            "alignof",
            "and",
            "and_eq",
            "asm",
            "atomic_cancel",
            "atomic_commit",
            "atomic_noexcept",
            "bitand",
            "bitor",
            "break",
            "case",
            "catch",
            "class",
            "compl",
            "concept",
            "const",
            "consteval",
            "constexpr",
            "constinit",
            "const_cast",
            "continue",
            "co_await",
            "co_return",
            "co_yield",
            "decltype",
            "default",
            "delete",
            "do",
            "dynamic_cast",
            "else",
            "enum",
            "explicit",
            "export",
            "extern",
            "false",
            "for",
            "friend",
            "goto",
            "if",
            "inline",
            "mutable",
            "namespace",
            "new",
            "noexcept",
            "not",
            "not_eq",
            "nullptr",
            "operator",
            "or",
            "or_eq",
            "private",
            "protected",
            "public",
            "reflexpr",
            "register",
            "reinterpret_cast",
            "requires",
            "return",
            "sizeof",
            "static",
            "static_assert",
            "static_cast",
            "struct",
            "switch",
            "synchronized",
            "template",
            "this",
            "thread_local",
            "throw",
            "true",
            "try",
            "typedef",
            "typeid",
            "typename",
            "union",
            "using",
            "virtual",
            "volatile",
            "while",
            "xor",
            "xor_eq",
        };
        for (size_t i = 0; i < sizeof(keywords) / sizeof(*keywords); ++i) {
            if (matches(contents, token->start, token->end, keywords[i])) {
                token->type = Token_Type::KEYWORD;
                return true;
            }
        }

        cz::Str type_keywords[] = {
            "auto", "bool", "char",  "char8_t", "char16_t", "char32_t", "double",  "float",
            "int",  "long", "short", "signed",  "unsigned", "void",     "wchar_t",
        };
        for (size_t i = 0; i < sizeof(type_keywords) / sizeof(*type_keywords); ++i) {
            if (matches(contents, token->start, token->end, type_keywords[i])) {
                token->type = Token_Type::TYPE;
                return true;
            }
        }

        // generic identifier
        token->type = Token_Type::IDENTIFIER;
        return true;
    }

    if (first_char == '/' && point + 1 < contents->len() && (*contents)[point + 1] == '/') {
        token->start = point;
        // TODO: Replace with end_of_line if we reform it to take Contents*
        while (point < contents->len() && (*contents)[point] != '\n') {
            ++point;
        }
        token->end = point;
        token->type = Token_Type::COMMENT;
        return true;
    }

    if (first_char == '/' && point + 1 < contents->len() && (*contents)[point + 1] == '*') {
        token->start = point;
        point += 2;
        while (point < contents->len()) {
            if (point + 1 < contents->len() && (*contents)[point] == '*' &&
                (*contents)[point + 1] == '/') {
                point += 2;
                break;
            }
            ++point;
        }
        token->end = point;
        token->type = Token_Type::COMMENT;
        return true;
    }

    token->start = point;
    token->end = point + 1;
    token->type = Token_Type::DEFAULT;
    return true;
}
}
