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

enum State : uint64_t {
    START_OF_STATEMENT = 0,
    IN_EXPR,
    IN_VARIABLE_TYPE,
    AFTER_VARIABLE_DECLARATION,
    START_OF_PARAMETER,
    IN_PARAMETER_TYPE,
    AFTER_PARAMETER_DECLARATION,
};

bool cpp_next_token(const Contents* contents, uint64_t point, Token* token, uint64_t* state) {
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
        *state = IN_EXPR;
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
        *state = IN_EXPR;
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

        cz::Str type_keywords[] = {"auto",           "bool",           "char",
                                   "char16_t",       "char32_t",       "char8_t",
                                   "double",         "float",          "int",
                                   "int16_t",        "int32_t",        "int64_t",
                                   "int8_t",         "int_fast16_t",   "int_fast32_t",
                                   "int_fast64_t",   "int_fast8_t",    "int_least16_t",
                                   "int_least32_t",  "int_least64_t",  "int_least8_t",
                                   "intmax_t",       "intptr_t",       "long",
                                   "ptrdiff_t",      "short",          "signed",
                                   "size_t",         "uint16_t",       "uint32_t",
                                   "uint64_t",       "uint8_t",        "uint_fast16_t",
                                   "uint_fast32_t",  "uint_fast64_t",  "uint_fast8_t",
                                   "uint_least16_t", "uint_least32_t", "uint_least64_t",
                                   "uint_least8_t",  "uintmax_t",      "uintptr_t",
                                   "unsigned",       "void",           "wchar_t"};
        for (size_t i = 0; i < sizeof(type_keywords) / sizeof(*type_keywords); ++i) {
            if (matches(contents, token->start, token->end, type_keywords[i])) {
                token->type = Token_Type::TYPE;
                if (*state == START_OF_PARAMETER) {
                    *state = IN_PARAMETER_TYPE;
                } else {
                    *state = IN_VARIABLE_TYPE;
                }
                return true;
            }
        }

        // generic identifier
        token->type = Token_Type::IDENTIFIER;

        if (*state == START_OF_STATEMENT || *state == START_OF_PARAMETER) {
            uint64_t temp_state = 0;
            Token next_token;
            cpp_next_token(contents, token->end, &next_token, &temp_state);
            if (next_token.type == Token_Type::IDENTIFIER ||
                (next_token.end == next_token.start + 1 &&
                 ((*contents)[next_token.start] == '*' || (*contents)[next_token.start] == '&'))) {
                if (*state == START_OF_STATEMENT) {
                    *state = IN_VARIABLE_TYPE;
                } else {
                    *state = IN_PARAMETER_TYPE;
                }
                token->type = Token_Type::TYPE;
            } else if (*state == START_OF_PARAMETER && next_token.end == next_token.start + 1 &&
                       (*contents)[next_token.start] == ',') {
                *state = AFTER_PARAMETER_DECLARATION;
                token->type = Token_Type::TYPE;
            }
        } else if (*state == IN_VARIABLE_TYPE) {
            *state = AFTER_VARIABLE_DECLARATION;
        } else if (*state == IN_PARAMETER_TYPE) {
            *state = AFTER_PARAMETER_DECLARATION;
        }

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

    if (ispunct(first_char)) {
        token->start = point;
        token->end = point + 1;
        if (first_char == '(' || first_char == '{' || first_char == '[') {
            token->type = Token_Type::OPEN_PAIR;
        } else if (first_char == ')' || first_char == '}' || first_char == ']') {
            token->type = Token_Type::CLOSE_PAIR;
        } else {
            token->type = Token_Type::PUNCTUATION;
        }

        if (first_char == ';' || first_char == '{' || first_char == '}') {
            *state = START_OF_STATEMENT;
        } else if (*state == START_OF_STATEMENT) {
            *state = IN_EXPR;
        } else if (*state == AFTER_VARIABLE_DECLARATION && first_char == '(') {
            *state = START_OF_PARAMETER;
        } else if (*state == AFTER_PARAMETER_DECLARATION && first_char == ',') {
            *state = START_OF_PARAMETER;
        }
        return true;
    }

    token->start = point;
    token->end = point + 1;
    token->type = Token_Type::DEFAULT;
    return true;
}

}
