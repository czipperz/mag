#include "tokenize_cpp.hpp"

#include <ctype.h>
#include <Tracy.hpp>
#include "contents.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

enum State : uint64_t {
    NORMAL_STATE_MASK = 0x000000000000000F,
    START_OF_STATEMENT = 0x0000000000000000,
    IN_EXPR = 0x0000000000000001,
    IN_VARIABLE_TYPE = 0x0000000000000002,
    AFTER_VARIABLE_DECLARATION = 0x0000000000000003,
    START_OF_PARAMETER = 0x0000000000000004,
    IN_PARAMETER_TYPE = 0x0000000000000005,
    AFTER_PARAMETER_DECLARATION = 0x0000000000000006,
    IN_TYPE_DEFINITION = 0x0000000000000007,
    AFTER_FOR = 0x0000000000000008,

    IN_PREPROCESSOR_FLAG = 0x0000000000000800,

    PREPROCESSOR_SAVED_STATE_MASK = 0x00000000000000F0,
    PREPROCESSOR_SAVE_SHIFT = 4,

    PREPROCESSOR_STATE_MASK = 0x0000000000000700,
    PREPROCESSOR_START_STATEMENT = 0x0000000000000000,
    PREPROCESSOR_AFTER_INCLUDE = 0x0000000000000100,
    PREPROCESSOR_AFTER_DEFINE = 0x0000000000000200,
    PREPROCESSOR_AFTER_DEFINE_NAME = 0x0000000000000300,
    PREPROCESSOR_IN_DEFINE_PARAMETERS = 0x0000000000000400,
    PREPROCESSOR_GENERAL = 0x0000000000000500,

    COMMENT_STATE_TO_SAVE_MASK = 0x0000000000000FFF,
    COMMENT_SAVED_STATE_MASK = 0x0000000000FFF000,
    COMMENT_SAVE_SHIFT = 12,

    COMMENT_MULTILINE_FLAG = 0x1000000000000000,
    COMMENT_ONELINE_FLAG = 0x2000000000000000,
    COMMENT_PREVIOUS_ONELINE_FLAG = 0x4000000000000000,

    COMMENT_STATE_MASK = 0x0700000000000000,
    COMMENT_START_OF_LINE = 0x0000000000000000,
    COMMENT_TITLE = 0x0100000000000000,
    COMMENT_MIDDLE_OF_LINE = 0x0200000000000000,
    COMMENT_CODE_INLINE = 0x0300000000000000,
    COMMENT_CODE_MULTILINE = 0x0400000000000000,
};

static bool skip_whitespace(Contents_Iterator* iterator,
                            char* ch,
                            bool* in_preprocessor,
                            bool* in_oneline_comment,
                            uint64_t* normal_state,
                            uint64_t* preprocessor_state,
                            uint64_t preprocessor_saved_state,
                            uint64_t* comment_state) {
    ZoneScoped;

    if (*in_preprocessor) {
        for (;; iterator->advance()) {
            if (iterator->at_eob()) {
                return false;
            }

            *ch = iterator->get();
            if (*ch == '\\') {
                iterator->advance();
                if (iterator->at_eob()) {
                    return false;
                }

                while (*ch = iterator->get(), *ch == '\\' || isblank(*ch)) {
                    iterator->advance();
                    if (iterator->at_eob()) {
                        return false;
                    }
                }

                if (*ch == '\n') {
                    continue;
                }
            }

            if (!isspace(*ch)) {
                return true;
            }

            if (*ch == '\n') {
                *in_preprocessor = false;
                *normal_state = preprocessor_saved_state >> PREPROCESSOR_SAVE_SHIFT;
                goto not_preprocessor;
            }

            if (*preprocessor_state == PREPROCESSOR_AFTER_DEFINE_NAME) {
                *preprocessor_state = PREPROCESSOR_GENERAL;
            }
        }
    } else {
    not_preprocessor:
        for (;; iterator->advance()) {
            if (iterator->at_eob()) {
                return false;
            }

            *ch = iterator->get();
            if (*ch == '\\') {
                iterator->advance();
                if (iterator->at_eob()) {
                    return false;
                }

                while (*ch = iterator->get(), *ch == '\\' || isblank(*ch)) {
                    iterator->advance();
                    if (iterator->at_eob()) {
                        return false;
                    }
                }

                if (*ch == '\n') {
                    continue;
                }
            }

            if (*ch == '\n') {
                *in_oneline_comment = false;
                if (*comment_state == COMMENT_TITLE || *comment_state == COMMENT_MIDDLE_OF_LINE) {
                    *comment_state = COMMENT_START_OF_LINE;
                }
            }
            if (!isspace(*ch)) {
                return true;
            }
        }
    }
}

static bool matches_no_bounds_check(const Contents* contents, Contents_Iterator it, cz::Str query) {
    ZoneScoped;

    for (size_t i = 0; i < query.len; ++i) {
        if (it.get() != query[i]) {
            return false;
        }
        it.advance();
    }
    return true;
}

static bool matches(const Contents* contents, Contents_Iterator it, uint64_t end, cz::Str query) {
    ZoneScoped;

    if (end - it.position != query.len) {
        return false;
    }
    if (it.position + query.len > contents->len) {
        return false;
    }
    return matches_no_bounds_check(contents, it, query);
}

static bool is_identifier_continuation(char ch) {
    return isalnum(ch) || ch == '_';
}

#define LOAD_COMBINED_STATE(SC)                                          \
    do {                                                                 \
        in_preprocessor = IN_PREPROCESSOR_FLAG & (SC);                   \
        normal_state = NORMAL_STATE_MASK & (SC);                         \
        preprocessor_state = PREPROCESSOR_STATE_MASK & (SC);             \
        preprocessor_saved_state = PREPROCESSOR_SAVED_STATE_MASK & (SC); \
        in_multiline_comment = COMMENT_MULTILINE_FLAG & (SC);            \
        in_oneline_comment = COMMENT_ONELINE_FLAG & (SC);                \
        previous_in_oneline_comment = in_oneline_comment;                \
        comment_state = COMMENT_STATE_MASK & (SC);                       \
        comment_saved_state = COMMENT_SAVED_STATE_MASK & (SC);           \
    } while (0)

#define MAKE_COMBINED_STATE(SC)                                                               \
    do {                                                                                      \
        (SC) = normal_state | preprocessor_state | preprocessor_saved_state | comment_state | \
               comment_saved_state;                                                           \
        if (in_preprocessor) {                                                                \
            (SC) |= IN_PREPROCESSOR_FLAG;                                                     \
        }                                                                                     \
        if (in_multiline_comment) {                                                           \
            (SC) |= COMMENT_MULTILINE_FLAG;                                                   \
        }                                                                                     \
        if (in_oneline_comment) {                                                             \
            (SC) |= COMMENT_ONELINE_FLAG;                                                     \
        }                                                                                     \
        if (previous_in_oneline_comment) {                                                    \
            (SC) |= COMMENT_PREVIOUS_ONELINE_FLAG;                                            \
        }                                                                                     \
    } while (0)

// Keyword lookup table macros.
#define LEN(L, BODY)  \
    case L:           \
        switch (ch) { \
            BODY;     \
        default:      \
            break;    \
        }             \
        break

#define CASE(CHAR, STR) \
    case CHAR:          \
        MATCHES(STR);   \
        break

#define MATCHES(STR)                                        \
    if (matches_no_bounds_check(contents, iterator, STR)) { \
        return true;                                        \
    }

#define ADVANCE(CHAR, BODY)     \
    case CHAR:                  \
        iterator.advance();     \
        switch (iterator.get()) \
            BODY;               \
        break

#define AT_OFFSET(CHAR, OFFSET, BODY)    \
    case CHAR: {                         \
        Contents_Iterator mi = iterator; \
        mi.advance(OFFSET);              \
        switch (mi.get())                \
            BODY;                        \
        break;                           \
    }

static bool look_for_type_definition_keyword(const Contents* contents,
                                             Contents_Iterator iterator,
                                             Token* token,
                                             char ch) {
    ZoneScoped;

    switch (ch) {
    case 'c':
        if (matches(contents, iterator, token->end, "class")) {
            return true;
        }
        break;
    case 'e':
        if (matches(contents, iterator, token->end, "enum")) {
            return true;
        }
        break;
    case 'u':
        if (matches(contents, iterator, token->end, "union")) {
            return true;
        }
        break;
    case 's':
        if (matches(contents, iterator, token->end, "struct")) {
            return true;
        }
        break;
    }
    return false;
}

static bool look_for_normal_keyword(const Contents* contents,
                                    Contents_Iterator iterator,
                                    Token* token,
                                    char ch) {
    ZoneScoped;

    switch (token->end - token->start) {
        LEN(2, {
            CASE('d', "do");
            CASE('i', "if");
            CASE('o', "or");
        });

        LEN(3, {
            ADVANCE('a', {
                CASE('n', "nd");
                CASE('s', "sm");
            });

            ADVANCE('n', {
                CASE('e', "ew");
                CASE('o', "ot");
            });

            CASE('t', "try");
            CASE('x', "xor");
        });

        LEN(4, {
            CASE('c', "case");
            CASE('e', "else");
            CASE('g', "goto");

            ADVANCE('t', {
                CASE('h', "his");
                CASE('r', "rue");
            });
        });

        LEN(5, {
            ADVANCE('b', {
                CASE('i', "itor");
                CASE('r', "reak");
            });

            AT_OFFSET('c', 4, {
                CASE('h', "catch");
                CASE('l', "compl");
                CASE('t', "const");
            });

            CASE('f', "false");
            CASE('o', "or_eq");
            CASE('t', "throw");
            CASE('u', "using");
            CASE('w', "while");
        });

        LEN(6, {
            CASE('a', "and_eq");
            CASE('b', "bitand");
            CASE('d', "delete");

            ADVANCE('e', {
                ADVANCE('x', {
                    CASE('p', "port");
                    CASE('t', "tern");
                });
            });

            CASE('f', "friend");
            CASE('i', "inline");
            CASE('n', "not_eq");
            CASE('p', "public");
            CASE('r', "return");

            ADVANCE('s', {
                CASE('i', "izeof");
                CASE('t', "tatic");
                CASE('w', "witch");
            });

            CASE('t', "typeid");
            CASE('x', "xor_eq");
        });

        LEN(7, {
            AT_OFFSET('a', 5, {
                CASE('a', "alignas");
                CASE('o', "alignof");
            });

            CASE('c', "concept");
            CASE('d', "default");
            CASE('m', "mutable");
            CASE('n', "nullptr");
            CASE('p', "private");
            CASE('t', "typedef");
            CASE('v', "virtual");
        });

        LEN(8, {
            AT_OFFSET('c', 3, {
                CASE('a', "co_await");
                CASE('y', "co_yield");
                CASE('t', "continue");
            });

            CASE('d', "decltype");
            CASE('e', "explicit");
            CASE('n', "noexcept");
            CASE('o', "operator");

            AT_OFFSET('r', 2, {
                CASE('f', "reflexpr");
                CASE('g', "register");
                CASE('q', "requires");
            });

            ADVANCE('t', {
                CASE('e', "emplate");
                CASE('y', "ypename");
            });

            CASE('v', "volatile");
        });

        LEN(9, {
            AT_OFFSET('c', 6, {
                CASE('u', "co_return");
                CASE('v', "consteval");
                CASE('x', "constexpr");
                CASE('n', "constinit");
            });

            CASE('n', "namespace");
            CASE('p', "protected");
        });

        LEN(10, CASE('c', "const_cast"));
        LEN(11, CASE('s', "static_cast"));

        LEN(12, {
            CASE('d', "dynamic_cast");
            CASE('s', "synchronized");
            CASE('t', "thread_local");
        });

        LEN(13, {
            AT_OFFSET('a', 8, {
                CASE('a', "atomic_cancel");
                CASE('o', "atomic_commit");
            });

            CASE('s', "static_assert");
        });

        LEN(15, CASE('a', "atomic_noexcept"));

        LEN(16, CASE('r', "reinterpret_cast"));
    }
    return false;
}

static bool look_for_type_keyword(const Contents* contents,
                                  Contents_Iterator iterator,
                                  Token* token,
                                  char ch) {
    ZoneScoped;

    switch (token->end - token->start) {
        LEN(3, CASE('i', "int"));
        LEN(4, {
            CASE('a', "int");
            CASE('b', "bool");
            CASE('c', "char");
            CASE('l', "long");
            CASE('v', "void");
        });

        LEN(5, {
            CASE('f', "float");
            CASE('s', "short");
        });

        LEN(6, {
            CASE('d', "double");
            CASE('i', "int8_t");

            AT_OFFSET('s', 2, {
                CASE('g', "signed");
                CASE('z', "size_t");
            });
        });

        LEN(7, {
            CASE('c', "char8_t");

            AT_OFFSET('i', 3, {
                CASE('1', "int16_t");
                CASE('3', "int32_t");
                CASE('6', "int64_t");
            });

            CASE('u', "uint8_t");
            CASE('w', "wchar_t");
        });

        LEN(8, {
            AT_OFFSET('c', 4, {
                CASE('1', "char16_t");
                CASE('3', "char32_t");
            });

            AT_OFFSET('i', 3, {
                CASE('m', "intmax_t");
                CASE('p', "intptr_t");
            });

            AT_OFFSET('u', 4, {
                CASE('1', "uint16_t");
                CASE('3', "uint32_t");
                CASE('6', "uint64_t");
                CASE('g', "unsigned");
            });
        });

        LEN(9, {
            CASE('p', "ptrdiff_t");

            AT_OFFSET('u', 4, {
                CASE('m', "uintmax_t");
                CASE('p', "uintptr_t");
            });
        });

        LEN(11, CASE('i', "int_fast8_t"));

        LEN(12, {
            AT_OFFSET('i', 8, {
                CASE('1', "int_fast16_t");
                CASE('3', "int_fast32_t");
                CASE('6', "int_fast64_t");
                CASE('t', "int_least8_t");
            });

            CASE('u', "uint_fast8_t");
        });

        LEN(13, {
            AT_OFFSET('i', 9, {
                CASE('1', "int_least16_t");
                CASE('3', "int_least32_t");
                CASE('6', "int_least64_t");
            });

            AT_OFFSET('u', 9, {
                CASE('1', "uint_fast16_t");
                CASE('3', "uint_fast32_t");
                CASE('6', "uint_fast64_t");
                CASE('t', "uint_least8_t");
            });
        });

        LEN(14, {
            AT_OFFSET('u', 10, {
                CASE('1', "uint_least16_t");
                CASE('3', "uint_least32_t");
                CASE('6', "uint_least64_t");
            });
        });
    }

    return false;
}

static void continue_inside_oneline_comment(Contents_Iterator* iterator,
                                            bool* in_oneline_comment,
                                            uint64_t* comment_state) {
    for (bool continue_into_next_line = false; !iterator->at_eob(); iterator->advance()) {
        char ch = iterator->get();
        if (ch == '\n') {
            if (!continue_into_next_line) {
                *in_oneline_comment = false;
                break;
            }
            continue_into_next_line = false;
        } else if (ch == '\\') {
            continue_into_next_line = true;
        } else if (isblank(ch)) {
        } else if (ch == '`') {
            *comment_state = COMMENT_MIDDLE_OF_LINE;
            break;
        } else if (*comment_state == COMMENT_START_OF_LINE &&
                   (ch == '#' || ch == '*' || ch == '-' || ch == '+')) {
            break;
        } else {
            continue_into_next_line = false;
        }
    }
}

static void continue_inside_multiline_comment(Contents_Iterator* iterator,
                                              bool* in_multiline_comment,
                                              uint64_t* comment_state) {
    char previous = 0;
    for (; !iterator->at_eob(); iterator->advance()) {
        char ch = iterator->get();
        if (previous == '*' && ch == '/') {
end_comment:
            iterator->advance();
            *in_multiline_comment = false;
            break;
        } else if (ch == '\n') {
            *comment_state = COMMENT_START_OF_LINE;
        } else if (ch == '`') {
            *comment_state = COMMENT_MIDDLE_OF_LINE;
            break;
        } else if (*comment_state == COMMENT_START_OF_LINE &&
                   (ch == '#' || ch == '*' || ch == '-' || ch == '+')) {
            Contents_Iterator it = *iterator;
            it.advance();
            if (ch == '*' && !it.at_eob() && it.get() == '/') {
*iterator = it;
                goto end_comment;
            }
            break;
        }

        previous = ch;
    }
}

static void continue_inside_multiline_comment_title(Contents_Iterator* iterator,
                                                    bool* in_multiline_comment,
                                                    uint64_t* comment_state) {
    char previous = 0;
    for (; !iterator->at_eob(); iterator->advance()) {
        char ch = iterator->get();
        if (previous == '*' && ch == '/') {
            iterator->advance();
            *in_multiline_comment = false;
            break;
        } else if (ch == '\n') {
            break;
        } else if (ch == '`') {
            *comment_state = COMMENT_MIDDLE_OF_LINE;
            break;
        } else if (*comment_state == COMMENT_START_OF_LINE &&
                   (ch == '#' || ch == '*' || ch == '-' || ch == '+')) {
            break;
        }

        previous = ch;
    }
}

bool cpp_next_token(const Contents* contents,
                    Contents_Iterator* iterator,
                    Token* token,
                    uint64_t* state_combined) {
    ZoneScoped;

    bool in_preprocessor;
    uint64_t normal_state;
    uint64_t preprocessor_state;
    uint64_t preprocessor_saved_state;
    bool in_multiline_comment;
    bool in_oneline_comment;
    bool previous_in_oneline_comment;
    uint64_t comment_state;
    uint64_t comment_saved_state;
    LOAD_COMBINED_STATE(*state_combined);

    char first_char;
    if (!skip_whitespace(iterator, &first_char, &in_preprocessor, &in_oneline_comment,
                         &normal_state, &preprocessor_state, preprocessor_saved_state,
                         &comment_state)) {
        return false;
    }

    if (in_oneline_comment || in_multiline_comment) {
        // The fact that we stopped here in the middle of a comment means we hit a special
        // character.  As of right now that is one of `, #, *, -, or +.
        switch (comment_state) {
        case COMMENT_START_OF_LINE:
            switch (first_char) {
            case '#':
                comment_state = COMMENT_TITLE;
                token->start = iterator->position;
                iterator->advance();
                while (!iterator->at_eob() && iterator->get() == '#') {
                    iterator->advance();
                }
                token->end = iterator->position;
                token->type = Token_Type::PUNCTUATION;
                break;
            case '*':
            case '-':
            case '+':
                comment_state = COMMENT_MIDDLE_OF_LINE;
                token->start = iterator->position;
                iterator->advance();
                token->end = iterator->position;
                token->type = Token_Type::PUNCTUATION;
                break;
            default:
                goto comment_normal;
            }
            goto done;

        case COMMENT_TITLE:
            token->start = iterator->position;
            if (in_oneline_comment) {
                continue_inside_oneline_comment(iterator, &in_oneline_comment, &comment_state);
            } else {
                continue_inside_multiline_comment_title(iterator, &in_multiline_comment,
                                                        &comment_state);
            }
            token->end = iterator->position;
            token->type = Token_Type::TITLE;
            goto done;

        case COMMENT_MIDDLE_OF_LINE:
            if (first_char == '`') {
                token->start = iterator->position;
                iterator->advance();
                comment_state = COMMENT_CODE_INLINE;
                if (!iterator->at_eob() && iterator->get() == '`') {
                    iterator->advance();
                    if (!iterator->at_eob() && iterator->get() == '`') {
                        iterator->advance();
                        comment_state = COMMENT_CODE_MULTILINE;
                    } else {
                        comment_state = COMMENT_MIDDLE_OF_LINE;
                    }
                }

                if (comment_state == COMMENT_CODE_INLINE ||
                    comment_state == COMMENT_CODE_MULTILINE) {
                    MAKE_COMBINED_STATE(*state_combined);
                    *state_combined &= ~COMMENT_SAVED_STATE_MASK;
                    *state_combined |= (*state_combined & COMMENT_STATE_TO_SAVE_MASK)
                                       << COMMENT_SAVE_SHIFT;
                    *state_combined &= ~COMMENT_STATE_TO_SAVE_MASK;
                    LOAD_COMBINED_STATE(*state_combined);
                }

                token->end = iterator->position;
                token->type = Token_Type::CODE;
                goto done;
            } else {
            comment_normal:
                token->start = iterator->position;
                if (in_oneline_comment) {
                    continue_inside_oneline_comment(iterator, &in_oneline_comment, &comment_state);
                } else {
                    continue_inside_multiline_comment(iterator, &in_multiline_comment,
                                                      &comment_state);
                }
                token->end = iterator->position;
                token->type = Token_Type::COMMENT;
                goto done;
            }

        case COMMENT_CODE_INLINE:
            if (first_char == '`') {
                token->start = iterator->position;
                iterator->advance();
                token->end = iterator->position;

            end_comment_code:
                MAKE_COMBINED_STATE(*state_combined);
                *state_combined &= ~COMMENT_STATE_TO_SAVE_MASK;
                *state_combined |=
                    (*state_combined & COMMENT_SAVED_STATE_MASK) >> COMMENT_SAVE_SHIFT;
                LOAD_COMBINED_STATE(*state_combined);

                comment_state = COMMENT_MIDDLE_OF_LINE;
                token->type = Token_Type::CODE;
                goto done;
            }
            break;

        case COMMENT_CODE_MULTILINE: {
            if (first_char == '`') {
                Contents_Iterator it = *iterator;
                it.advance();
                if (!it.at_eob() && it.get() == '`') {
                    it.advance();
                    if (!it.at_eob() && it.get() == '`') {
                        token->start = iterator->position;
                        it.advance();
                        *iterator = it;
                        token->end = iterator->position;
                        goto end_comment_code;
                    }
                }
            }
        }
        }
    }

    if (first_char == '#' && !in_preprocessor) {
        ZoneScopedN("preprocessor #");
        in_preprocessor = true;
        preprocessor_state = PREPROCESSOR_START_STATEMENT;
        preprocessor_saved_state = normal_state << PREPROCESSOR_SAVE_SHIFT;
        normal_state = IN_EXPR;

        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
        token->type = Token_Type::PUNCTUATION;
        goto done;
    }

    if (first_char == '"' || (first_char == '<' && in_preprocessor &&
                              preprocessor_state == PREPROCESSOR_AFTER_INCLUDE)) {
        ZoneScopedN("string");
        token->start = iterator->position;
        for (iterator->advance(); !iterator->at_eob(); iterator->advance()) {
            char ch = iterator->get();
            if (ch == '"' || ch == '\n') {
                iterator->advance();
                break;
            }
            if (ch == '>' && in_preprocessor && preprocessor_state == PREPROCESSOR_AFTER_INCLUDE) {
                preprocessor_state = PREPROCESSOR_GENERAL;
                iterator->advance();
                break;
            }
            if (ch == '\\') {
                if (iterator->position + 1 < contents->len) {
                    // Only skip over the next character if we don't go out of bounds.
                    iterator->advance();

                    // Skip all spaces and then we eat newline in the outer loop.
                    while (!iterator->at_eob() && isblank(iterator->get())) {
                        iterator->advance();
                    }

                    if (iterator->at_eob()) {
                        break;
                    }
                }
            }
        }
        token->end = iterator->position;
        token->type = Token_Type::STRING;
        normal_state = IN_EXPR;
        goto done;
    }

    if (first_char == '\'') {
        ZoneScopedN("character");
        token->start = iterator->position;
        if (iterator->position + 3 >= contents->len) {
            iterator->advance(contents->len - iterator->position);
        } else {
            iterator->advance();
            if (iterator->get() == '\\') {
                iterator->advance(3);
            } else {
                iterator->advance(2);
            }
        }
        token->end = iterator->position;
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
        ZoneScopedN("identifier");

        Contents_Iterator start_iterator = *iterator;
        {
            ZoneScopedN("find end");
            token->start = iterator->position;
            for (iterator->advance();
                 !iterator->at_eob() && is_identifier_continuation(iterator->get());
                 iterator->advance()) {
            }
            token->end = iterator->position;
        }

        if (in_preprocessor && preprocessor_state == PREPROCESSOR_START_STATEMENT) {
            ZoneScopedN("preprocessor keyword");
            token->type = Token_Type::KEYWORD;
            if (matches(contents, start_iterator, token->end, "include")) {
                preprocessor_state = PREPROCESSOR_AFTER_INCLUDE;
                goto done_no_skip;
            } else if (matches(contents, start_iterator, token->end, "define")) {
                preprocessor_state = PREPROCESSOR_AFTER_DEFINE;
                goto done_no_skip;
            } else {
                preprocessor_state = PREPROCESSOR_GENERAL;
                goto done;
            }
        }

        if (in_preprocessor && preprocessor_state == PREPROCESSOR_AFTER_DEFINE) {
            ZoneScopedN("preprocessor definition");
            preprocessor_state = PREPROCESSOR_AFTER_DEFINE_NAME;
            normal_state = START_OF_STATEMENT;
            token->type = Token_Type::IDENTIFIER;
            goto done;
        }

        if (look_for_type_definition_keyword(contents, start_iterator, token, first_char)) {
            token->type = Token_Type::KEYWORD;
            normal_state = IN_TYPE_DEFINITION;
            goto done;
        }

        if (matches(contents, start_iterator, token->end, "for")) {
            token->type = Token_Type::KEYWORD;
            normal_state = AFTER_FOR;
            goto done;
        }

        if (look_for_normal_keyword(contents, start_iterator, token, first_char)) {
            token->type = Token_Type::KEYWORD;
            goto done;
        }

        if (look_for_type_keyword(contents, start_iterator, token, first_char)) {
            token->type = Token_Type::TYPE;
            if (normal_state == START_OF_PARAMETER) {
                normal_state = IN_PARAMETER_TYPE;
            } else {
                normal_state = IN_VARIABLE_TYPE;
            }
            goto done;
        }

        // generic identifier
        token->type = Token_Type::IDENTIFIER;

        if (normal_state == START_OF_STATEMENT || normal_state == START_OF_PARAMETER) {
            ZoneScopedN("look for name of variable");
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
            Contents_Iterator next_token_iterator = *iterator;
            if (!cpp_next_token(contents, &next_token_iterator, &next_token, &temp_state)) {
                // couldn't get next token
            } else if (in_preprocessor && !(temp_state & IN_PREPROCESSOR_FLAG)) {
                // next token is outside preprocessor invocation we are in
            } else {
                Contents_Iterator it = next_token_iterator;
                CZ_DEBUG_ASSERT(it.position > next_token.start);
                it.retreat(next_token_iterator.position - next_token.start);
                char start_ch = it.get();

                bool is_type = false;
                if (next_token.type == Token_Type::IDENTIFIER) {
                    is_type = true;
                } else if (next_token.type == Token_Type::PUNCTUATION) {
                    if (next_token.end == next_token.start + 1) {
                        if (start_ch == '*' || start_ch == '&') {
                            is_type = true;
                        }
                    } else if (next_token.end == next_token.start + 2) {
                        it.advance();
                        char start_ch2 = it.get();
                        if (start_ch == ':' || start_ch2 == ':') {
                            if (normal_state == START_OF_PARAMETER) {
                                is_type = true;
                            } else {
                                // See if the part after the namespace is a type declaration.
                                if (!cpp_next_token(contents, &next_token_iterator, &next_token,
                                                    &temp_state)) {
                                    // couldn't get next token
                                } else if (in_preprocessor &&
                                           !(temp_state & IN_PREPROCESSOR_FLAG)) {
                                    // next token is outside preprocessor invocation we are in
                                } else if (next_token.type == Token_Type::TYPE) {
                                    is_type = true;
                                }
                            }
                        }
                    }
                }

                if (is_type) {
                    if (normal_state == START_OF_STATEMENT) {
                        normal_state = IN_VARIABLE_TYPE;
                    } else {
                        normal_state = IN_PARAMETER_TYPE;
                    }
                    token->type = Token_Type::TYPE;
                } else if (normal_state == START_OF_PARAMETER &&
                           next_token.end == next_token.start + 1 && start_ch == ',') {
                    normal_state = AFTER_PARAMETER_DECLARATION;
                    token->type = Token_Type::TYPE;
                } else {
                    normal_state = IN_EXPR;
                }
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

    if (first_char == '/') {
        Contents_Iterator next_iterator = *iterator;
        next_iterator.advance();
        if (!next_iterator.at_eob() && next_iterator.get() == '/') {
            ZoneScopedN("line comment");
            in_oneline_comment = true;
            token->start = iterator->position;
            next_iterator.advance();
            *iterator = next_iterator;
            if (previous_in_oneline_comment &&
                (comment_state == COMMENT_CODE_INLINE || comment_state == COMMENT_CODE_MULTILINE)) {
                // Merge consecutive online comments where a code block extends between them.
            } else {
                comment_state = COMMENT_START_OF_LINE;
                continue_inside_oneline_comment(iterator, &in_oneline_comment, &comment_state);
            }
            token->end = iterator->position;
            token->type = Token_Type::COMMENT;
            goto done;
        }

        if (!next_iterator.at_eob() && next_iterator.get() == '*') {
            ZoneScopedN("block comment");
            in_multiline_comment = true;
            comment_state = COMMENT_START_OF_LINE;
            token->start = iterator->position;
            next_iterator.advance();
            *iterator = next_iterator;
            continue_inside_multiline_comment(iterator, &in_multiline_comment, &comment_state);
            token->end = iterator->position;
            token->type = Token_Type::COMMENT;
            goto done;
        }
    }

    if (ispunct(first_char)) {
        ZoneScopedN("punctuation");
        token->start = iterator->position;
        iterator->advance();

        char second_char = 0;
        if (!iterator->at_eob()) {
            second_char = iterator->get();
            if (first_char == '#' && second_char == '#') {
                iterator->advance();
            } else if (first_char == '.' && second_char == '*') {
                iterator->advance();
            } else if (first_char == '.' && second_char == '.') {
                Contents_Iterator it = *iterator;
                it.advance();
                if (!it.at_eob()) {
                    if (it.get() == '.') {
                        it.advance();
                        *iterator = it;
                    }
                }
            } else if (first_char == ':' && second_char == ':') {
                iterator->advance();
            } else if (first_char == '-' && second_char == '>') {
                iterator->advance();
                if (!iterator->at_eob() && iterator->get() == '*') {
                    iterator->advance();
                }
            } else if ((first_char == '&' || first_char == '|' || first_char == '-' ||
                        first_char == '+' || first_char == '=') &&
                       second_char == first_char) {
                iterator->advance();
            } else if ((first_char == '<' || first_char == '>') && second_char == first_char) {
                iterator->advance();
                if (!iterator->at_eob() && iterator->get() == '=') {
                    iterator->advance();
                }
            } else if ((first_char == '!' || first_char == '>' || first_char == '<' ||
                        first_char == '+' || first_char == '-' || first_char == '*' ||
                        first_char == '/' || first_char == '%' || first_char == '&' ||
                        first_char == '|' || first_char == '^') &&
                       second_char == '=') {
                iterator->advance();
            }
        }
        token->end = iterator->position;

        if (first_char == '(' || first_char == '{' || first_char == '[') {
            token->type = Token_Type::OPEN_PAIR;
        } else if (first_char == ')' || first_char == '}' || first_char == ']') {
            token->type = Token_Type::CLOSE_PAIR;
        } else {
            token->type = Token_Type::PUNCTUATION;
        }

        if (first_char == ';' || first_char == '{' || first_char == '}') {
            normal_state = START_OF_STATEMENT;
        } else if (normal_state == IN_VARIABLE_TYPE && first_char == ':' && second_char == ':') {
            normal_state = START_OF_STATEMENT;
        } else if (normal_state == IN_PARAMETER_TYPE && first_char == ':' && second_char == ':') {
            normal_state = START_OF_PARAMETER;
        } else if (normal_state == START_OF_STATEMENT) {
            normal_state = IN_EXPR;
        } else if (normal_state == AFTER_FOR && first_char == '(') {
            normal_state = START_OF_STATEMENT;
        } else if (normal_state == AFTER_VARIABLE_DECLARATION && first_char == '(') {
            normal_state = START_OF_PARAMETER;
        } else if (normal_state == AFTER_VARIABLE_DECLARATION && first_char == '=') {
            normal_state = IN_EXPR;
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

    token->start = iterator->position;
    iterator->advance();
    token->end = iterator->position;
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
}
