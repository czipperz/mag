#include "tokenize_cpp.hpp"

#include <ctype.h>
#include "contents.hpp"
#include "token.hpp"

namespace mag {

enum State : uint64_t {
    IN_PREPROCESSOR_FLAG = 0x8000000000000000,

    NORMAL_STATE_MASK = 0x0000000000000007,
    START_OF_STATEMENT = 0x0000000000000000,
    IN_EXPR = 0x0000000000000001,
    IN_VARIABLE_TYPE = 0x0000000000000002,
    AFTER_VARIABLE_DECLARATION = 0x0000000000000003,
    START_OF_PARAMETER = 0x0000000000000004,
    IN_PARAMETER_TYPE = 0x0000000000000005,
    AFTER_PARAMETER_DECLARATION = 0x0000000000000006,
    IN_TYPE_DEFINITION = 0x0000000000000007,

    PREPROCESSOR_SAVED_STATE_MASK = 0x0000000000000038,
    PREPROCESSOR_SAVE_SHIFT = 3,

    PREPROCESSOR_STATE_MASK = 0x7000000000000000,
    PREPROCESSOR_START_STATEMENT = 0x0000000000000000,
    PREPROCESSOR_AFTER_INCLUDE = 0x1000000000000000,
    PREPROCESSOR_AFTER_DEFINE = 0x2000000000000000,
    PREPROCESSOR_AFTER_DEFINE_NAME = 0x3000000000000000,
    PREPROCESSOR_IN_DEFINE_PARAMETERS = 0x4000000000000000,
    PREPROCESSOR_GENERAL = 0x5000000000000000,
};

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

#define MAKE_COMBINED_STATE(SC)                                              \
    do {                                                                     \
        (SC) = normal_state | preprocessor_state | preprocessor_saved_state; \
        if (in_preprocessor) {                                               \
            (SC) |= IN_PREPROCESSOR_FLAG;                                    \
        }                                                                    \
    } while (0)

bool cpp_next_token(const Contents* contents,
                    uint64_t point,
                    Token* token,
                    uint64_t* state_combined) {
    bool in_preprocessor = *state_combined & IN_PREPROCESSOR_FLAG;
    uint64_t normal_state = *state_combined & NORMAL_STATE_MASK;
    uint64_t preprocessor_state = *state_combined & PREPROCESSOR_STATE_MASK;
    uint64_t preprocessor_saved_state = *state_combined & PREPROCESSOR_SAVED_STATE_MASK;

    while (point < contents->len() && isspace((*contents)[point])) {
        if ((*contents)[point] == '\n') {
            if (in_preprocessor) {
                in_preprocessor = false;
                normal_state = preprocessor_saved_state >> PREPROCESSOR_SAVE_SHIFT;
            }
        }
        ++point;
        if (in_preprocessor && preprocessor_state == PREPROCESSOR_AFTER_DEFINE_NAME) {
            preprocessor_state = PREPROCESSOR_GENERAL;
        }
    }

    if (point == contents->len()) {
        return false;
    }

    char first_char = (*contents)[point];

    if (first_char == '#') {
        in_preprocessor = true;
        preprocessor_state = PREPROCESSOR_START_STATEMENT;
        preprocessor_saved_state = normal_state << PREPROCESSOR_SAVE_SHIFT;
        normal_state = IN_EXPR;
        token->start = point;
        token->end = point + 1;
        token->type = Token_Type::PUNCTUATION;
        goto done;
    }

    if (first_char == '"' || (first_char == '<' && in_preprocessor &&
                              preprocessor_state == PREPROCESSOR_AFTER_INCLUDE)) {
        token->start = point;
        ++point;
        for (; point < contents->len(); ++point) {
            if ((*contents)[point] == '"') {
                ++point;
                break;
            }
            if ((*contents)[point] == '>' && in_preprocessor &&
                preprocessor_state == PREPROCESSOR_AFTER_INCLUDE) {
                preprocessor_state = PREPROCESSOR_GENERAL;
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
        normal_state = IN_EXPR;
        goto done;
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
        normal_state = IN_EXPR;
        goto done;
    }

    if (in_preprocessor && preprocessor_state == PREPROCESSOR_AFTER_DEFINE_NAME &&
        first_char != '(') {
        preprocessor_state = PREPROCESSOR_GENERAL;
        normal_state = START_OF_STATEMENT;
    }

    if (isalpha(first_char) || first_char == '_') {
        token->start = point;
        while (++point < contents->len() &&
               (isalnum((*contents)[point]) || (*contents)[point] == '_')) {
        }
        token->end = point;

        if (in_preprocessor && preprocessor_state == PREPROCESSOR_START_STATEMENT) {
            token->type = Token_Type::KEYWORD;
            if (matches(contents, token->start, token->end, "include")) {
                preprocessor_state = PREPROCESSOR_AFTER_INCLUDE;
                goto done_no_skip;
            } else if (matches(contents, token->start, token->end, "define")) {
                preprocessor_state = PREPROCESSOR_AFTER_DEFINE;
                goto done_no_skip;
            } else {
                preprocessor_state = PREPROCESSOR_GENERAL;
                goto done;
            }
        }

        if (in_preprocessor && preprocessor_state == PREPROCESSOR_AFTER_DEFINE) {
            preprocessor_state = PREPROCESSOR_AFTER_DEFINE_NAME;
            normal_state = START_OF_STATEMENT;
            token->type = Token_Type::IDENTIFIER;
            goto done;
        }

        cz::Str type_definition_keywords[] = {
            "class",
            "enum",
            "union",
            "struct",
        };
        for (size_t i = 0; i < sizeof(type_definition_keywords) / sizeof(*type_definition_keywords);
             ++i) {
            if (matches(contents, token->start, token->end, type_definition_keywords[i])) {
                token->type = Token_Type::KEYWORD;
                normal_state = IN_TYPE_DEFINITION;
                goto done;
            }
        }

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
                goto done;
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
                if (normal_state == START_OF_PARAMETER) {
                    normal_state = IN_PARAMETER_TYPE;
                } else {
                    normal_state = IN_VARIABLE_TYPE;
                }
                goto done;
            }
        }

        // generic identifier
        token->type = Token_Type::IDENTIFIER;

        if (normal_state == START_OF_STATEMENT || normal_state == START_OF_PARAMETER) {
            uint64_t temp_state;
            {
                uint64_t backup_normal_state = normal_state;
                if (normal_state == START_OF_STATEMENT) {
                    normal_state = IN_VARIABLE_TYPE;
                } else {
                    normal_state = IN_PARAMETER_TYPE;
                }
                MAKE_COMBINED_STATE(temp_state);
                normal_state = backup_normal_state;
            }

            Token next_token;
            if (!cpp_next_token(contents, token->end, &next_token, &temp_state)) {
                // couldn't get next token
            } else if (in_preprocessor && !(temp_state & IN_PREPROCESSOR_FLAG)) {
                // next token is outside preprocessor invocation we are in
            } else if (next_token.type == Token_Type::IDENTIFIER ||
                       (next_token.end == next_token.start + 1 &&
                        ((*contents)[next_token.start] == '*' ||
                         (*contents)[next_token.start] == '&'))) {
                if (normal_state == START_OF_STATEMENT) {
                    normal_state = IN_VARIABLE_TYPE;
                } else {
                    normal_state = IN_PARAMETER_TYPE;
                }
                token->type = Token_Type::TYPE;
            } else if (normal_state == START_OF_PARAMETER &&
                       next_token.end == next_token.start + 1 &&
                       (*contents)[next_token.start] == ',') {
                normal_state = AFTER_PARAMETER_DECLARATION;
                token->type = Token_Type::TYPE;
            }
        } else if (normal_state == IN_TYPE_DEFINITION) {
            normal_state = IN_EXPR;
            token->type = Token_Type::TYPE;
        } else if (normal_state == IN_VARIABLE_TYPE) {
            normal_state = AFTER_VARIABLE_DECLARATION;
        } else if (normal_state == IN_PARAMETER_TYPE) {
            normal_state = AFTER_PARAMETER_DECLARATION;
        }

        goto done;
    }

    if (first_char == '/' && point + 1 < contents->len() && (*contents)[point + 1] == '/') {
        token->start = point;

        for (bool continue_into_next_line = false; point < contents->len(); ++point) {
            if ((*contents)[point] == '\n') {
                if (!continue_into_next_line) {
                    break;
                }
                continue_into_next_line = false;
            } else if ((*contents)[point] == '\\') {
                continue_into_next_line = true;
            } else if (!isblank((*contents)[point])) {
                continue_into_next_line = false;
            }
        }

        token->end = point;
        token->type = Token_Type::COMMENT;
        goto done;
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
        goto done;
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
            normal_state = START_OF_STATEMENT;
        } else if (normal_state == START_OF_STATEMENT) {
            normal_state = IN_EXPR;
        } else if (normal_state == AFTER_VARIABLE_DECLARATION && first_char == '(') {
            normal_state = START_OF_PARAMETER;
        } else if (normal_state == AFTER_PARAMETER_DECLARATION && first_char == ',') {
            normal_state = START_OF_PARAMETER;
        }

        if (in_preprocessor) {
            if (first_char == '(' && preprocessor_state == PREPROCESSOR_AFTER_DEFINE_NAME) {
                preprocessor_state = PREPROCESSOR_IN_DEFINE_PARAMETERS;
                normal_state = IN_EXPR;
            } else if (first_char == ')' &&
                       preprocessor_state == PREPROCESSOR_IN_DEFINE_PARAMETERS) {
                preprocessor_state = PREPROCESSOR_GENERAL;
                normal_state = START_OF_STATEMENT;
            }
        }

        goto done;
    }

    token->start = point;
    token->end = point + 1;
    token->type = Token_Type::DEFAULT;
    goto done;

done:
    if (in_preprocessor && preprocessor_state == PREPROCESSOR_AFTER_INCLUDE &&
        preprocessor_state == PREPROCESSOR_AFTER_DEFINE &&
        preprocessor_state == PREPROCESSOR_AFTER_DEFINE_NAME) {
        preprocessor_state = PREPROCESSOR_GENERAL;
    }

done_no_skip:
    MAKE_COMBINED_STATE(*state_combined);
    return true;
}

}
