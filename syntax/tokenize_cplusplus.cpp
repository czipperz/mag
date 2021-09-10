#include "tokenize_cplusplus.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/string.hpp>
#include "contents.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

namespace tokenize_cpp_impl {
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

    COMMENT_MULTILINE_DOC_FLAG = 0x1000000000000000,
    COMMENT_MULTILINE_NORMAL_FLAG = 0x2000000000000000,
    COMMENT_ONELINE_FLAG = 0x4000000000000000,
    COMMENT_PREVIOUS_ONELINE_FLAG = 0x8000000000000000,

    COMMENT_STATE_MASK = 0x0700000000000000,
    COMMENT_START_OF_LINE_1 = 0x0000000000000000,
    COMMENT_START_OF_LINE_2 = 0x0100000000000000,
    COMMENT_TITLE = 0x0200000000000000,
    COMMENT_MIDDLE_OF_LINE = 0x0300000000000000,
    COMMENT_CODE_INLINE = 0x0400000000000000,
    COMMENT_CODE_MULTILINE = 0x0500000000000000,
};
}
using namespace tokenize_cpp_impl;

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

                while (*ch = iterator->get(), *ch == '\\' || cz::is_blank(*ch)) {
                    iterator->advance();
                    if (iterator->at_eob()) {
                        return false;
                    }
                }

                if (*ch == '\n') {
                    continue;
                }
            }

            if (!cz::is_space(*ch)) {
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

                while (*ch = iterator->get(), *ch == '\\' || cz::is_blank(*ch)) {
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
                    *comment_state = COMMENT_START_OF_LINE_1;
                }
            }
            if (!cz::is_space(*ch)) {
                return true;
            }
        }
    }
}

static bool is_identifier_continuation(char ch) {
    return cz::is_alnum(ch) || ch == '_';
}

#define LOAD_COMBINED_STATE(SC)                                             \
    do {                                                                    \
        in_preprocessor = IN_PREPROCESSOR_FLAG & (SC);                      \
        normal_state = NORMAL_STATE_MASK & (SC);                            \
        preprocessor_state = PREPROCESSOR_STATE_MASK & (SC);                \
        preprocessor_saved_state = PREPROCESSOR_SAVED_STATE_MASK & (SC);    \
        in_multiline_doc_comment = COMMENT_MULTILINE_DOC_FLAG & (SC);       \
        in_multiline_normal_comment = COMMENT_MULTILINE_NORMAL_FLAG & (SC); \
        in_oneline_comment = COMMENT_ONELINE_FLAG & (SC);                   \
        previous_in_oneline_comment = in_oneline_comment;                   \
        comment_state = COMMENT_STATE_MASK & (SC);                          \
        comment_saved_state = COMMENT_SAVED_STATE_MASK & (SC);              \
    } while (0)

#define MAKE_COMBINED_STATE(SC)                                                               \
    do {                                                                                      \
        (SC) = normal_state | preprocessor_state | preprocessor_saved_state | comment_state | \
               comment_saved_state;                                                           \
        if (in_preprocessor) {                                                                \
            (SC) |= IN_PREPROCESSOR_FLAG;                                                     \
        }                                                                                     \
        if (in_multiline_doc_comment) {                                                       \
            (SC) |= COMMENT_MULTILINE_DOC_FLAG;                                               \
        }                                                                                     \
        if (in_multiline_normal_comment) {                                                    \
            (SC) |= COMMENT_MULTILINE_NORMAL_FLAG;                                            \
        }                                                                                     \
        if (in_oneline_comment) {                                                             \
            (SC) |= COMMENT_ONELINE_FLAG;                                                     \
        }                                                                                     \
        if (previous_in_oneline_comment) {                                                    \
            (SC) |= COMMENT_PREVIOUS_ONELINE_FLAG;                                            \
        }                                                                                     \
    } while (0)

static int look_for_keyword(cz::Str identifier_string) {
    ZoneScoped;

    switch ((identifier_string.len << 8) | (uint8_t)identifier_string[0]) {
    case (2 << 8) | (uint8_t)'d':
        if (identifier_string == "do")
            return 2;
        return 0;
    case (2 << 8) | (uint8_t)'i':
        if (identifier_string == "if")
            return 2;
        return 0;
    case (2 << 8) | (uint8_t)'o':
        if (identifier_string == "or")
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'a':
        if (identifier_string == "and")
            return 2;
        // return 0;
        // case (3 << 8) | (uint8_t)'a':
        if (identifier_string == "asm")
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'f':
        if (identifier_string == "for")
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'i':
        if (identifier_string == "int")
            return 3;
        return 0;
    case (3 << 8) | (uint8_t)'n':
        if (identifier_string == "new")
            return 2;
        // return 0;
        // case (3 << 8) | (uint8_t)'n':
        if (identifier_string == "not")
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'t':
        if (identifier_string == "try")
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'x':
        if (identifier_string == "xor")
            return 2;
        return 0;
    case (4 << 8) | (uint8_t)'a':
        if (identifier_string == "auto")
            return 3;
        return 0;
    case (4 << 8) | (uint8_t)'b':
        if (identifier_string == "bool")
            return 3;
        return 0;
    case (4 << 8) | (uint8_t)'c':
        if (identifier_string == "case")
            return 2;
        // return 0;
        // case (4 << 8) | (uint8_t)'c':
        if (identifier_string == "char")
            return 3;
        return 0;
    case (4 << 8) | (uint8_t)'e':
        if (identifier_string == "else")
            return 2;
        // return 0;
        // case (4 << 8) | (uint8_t)'e':
        if (identifier_string == "enum")
            return 1;
        return 0;
    case (4 << 8) | (uint8_t)'g':
        if (identifier_string == "goto")
            return 2;
        return 0;
    case (4 << 8) | (uint8_t)'l':
        if (identifier_string == "long")
            return 3;
        return 0;
    case (4 << 8) | (uint8_t)'t':
        if (identifier_string == "this")
            return 2;
        // return 0;
        // case (4 << 8) | (uint8_t)'t':
        if (identifier_string == "true")
            return 2;
        return 0;
    case (4 << 8) | (uint8_t)'v':
        if (identifier_string == "void")
            return 3;
        return 0;
    case (5 << 8) | (uint8_t)'b':
        if (identifier_string == "bitor")
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'b':
        if (identifier_string == "break")
            return 2;
        return 0;
    case (5 << 8) | (uint8_t)'c':
        if (identifier_string == "catch")
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'c':
        if (identifier_string == "class")
            return 1;
        // return 0;
        // case (5 << 8) | (uint8_t)'c':
        if (identifier_string == "compl")
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'c':
        if (identifier_string == "const")
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'f':
        if (identifier_string == "false")
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'f':
        if (identifier_string == "float")
            return 3;
        return 0;
    case (5 << 8) | (uint8_t)'o':
        if (identifier_string == "or_eq")
            return 2;
        return 0;
    case (5 << 8) | (uint8_t)'s':
        if (identifier_string == "short")
            return 3;
        return 0;
    case (5 << 8) | (uint8_t)'t':
        if (identifier_string == "throw")
            return 2;
        return 0;
    case (5 << 8) | (uint8_t)'u':
        if (identifier_string == "union")
            return 1;
        // return 0;
        // case (5 << 8) | (uint8_t)'u':
        if (identifier_string == "using")
            return 2;
        return 0;
    case (5 << 8) | (uint8_t)'w':
        if (identifier_string == "while")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'a':
        if (identifier_string == "and_eq")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'b':
        if (identifier_string == "bitand")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'d':
        if (identifier_string == "delete")
            return 2;
        // return 0;
        // case (6 << 8) | (uint8_t)'d':
        if (identifier_string == "double")
            return 3;
        return 0;
    case (6 << 8) | (uint8_t)'e':
        if (identifier_string == "export")
            return 2;
        // return 0;
        // case (6 << 8) | (uint8_t)'e':
        if (identifier_string == "extern")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'f':
        if (identifier_string == "friend")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'i':
        if (identifier_string == "inline")
            return 2;
        // return 0;
        // case (6 << 8) | (uint8_t)'i':
        if (identifier_string == "int8_t")
            return 3;
        return 0;
    case (6 << 8) | (uint8_t)'n':
        if (identifier_string == "not_eq")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'p':
        if (identifier_string == "public")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'r':
        if (identifier_string == "return")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'s':
        if (identifier_string == "signed")
            return 3;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (identifier_string == "size_t")
            return 3;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (identifier_string == "sizeof")
            return 2;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (identifier_string == "static")
            return 2;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (identifier_string == "struct")
            return 1;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (identifier_string == "switch")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'t':
        if (identifier_string == "typeid")
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'x':
        if (identifier_string == "xor_eq")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'a':
        if (identifier_string == "alignas")
            return 2;
        // return 0;
        // case (7 << 8) | (uint8_t)'a':
        if (identifier_string == "alignof")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'c':
        if (identifier_string == "char8_t")
            return 3;
        // return 0;
        // case (7 << 8) | (uint8_t)'c':
        if (identifier_string == "concept")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'d':
        if (identifier_string == "default")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'i':
        if (identifier_string == "int16_t")
            return 3;
        // return 0;
        // case (7 << 8) | (uint8_t)'i':
        if (identifier_string == "int32_t")
            return 3;
        // return 0;
        // case (7 << 8) | (uint8_t)'i':
        if (identifier_string == "int64_t")
            return 3;
        return 0;
    case (7 << 8) | (uint8_t)'m':
        if (identifier_string == "mutable")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'n':
        if (identifier_string == "nullptr")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'p':
        if (identifier_string == "private")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'t':
        if (identifier_string == "typedef")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'u':
        if (identifier_string == "uint8_t")
            return 3;
        return 0;
    case (7 << 8) | (uint8_t)'v':
        if (identifier_string == "virtual")
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'w':
        if (identifier_string == "wchar_t")
            return 3;
        return 0;
    case (8 << 8) | (uint8_t)'c':
        if (identifier_string == "char16_t")
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'c':
        if (identifier_string == "char32_t")
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'c':
        if (identifier_string == "co_await")
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'c':
        if (identifier_string == "co_yield")
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'c':
        if (identifier_string == "continue")
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'d':
        if (identifier_string == "decltype")
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'e':
        if (identifier_string == "explicit")
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'i':
        if (identifier_string == "intmax_t")
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'i':
        if (identifier_string == "intptr_t")
            return 3;
        return 0;
    case (8 << 8) | (uint8_t)'n':
        if (identifier_string == "noexcept")
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'o':
        if (identifier_string == "operator")
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'r':
        if (identifier_string == "reflexpr")
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'r':
        if (identifier_string == "register")
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'r':
        if (identifier_string == "requires")
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'t':
        if (identifier_string == "template")
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'t':
        if (identifier_string == "typename")
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'u':
        if (identifier_string == "uint16_t")
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'u':
        if (identifier_string == "uint32_t")
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'u':
        if (identifier_string == "uint64_t")
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'u':
        if (identifier_string == "unsigned")
            return 3;
        return 0;
    case (8 << 8) | (uint8_t)'v':
        if (identifier_string == "volatile")
            return 2;
        return 0;
    case (9 << 8) | (uint8_t)'c':
        if (identifier_string == "co_return")
            return 2;
        // return 0;
        // case (9 << 8) | (uint8_t)'c':
        if (identifier_string == "consteval")
            return 2;
        // return 0;
        // case (9 << 8) | (uint8_t)'c':
        if (identifier_string == "constexpr")
            return 2;
        // return 0;
        // case (9 << 8) | (uint8_t)'c':
        if (identifier_string == "constinit")
            return 2;
        return 0;
    case (9 << 8) | (uint8_t)'n':
        if (identifier_string == "namespace")
            return 2;
        return 0;
    case (9 << 8) | (uint8_t)'p':
        if (identifier_string == "protected")
            return 2;
        // return 0;
        // case (9 << 8) | (uint8_t)'p':
        if (identifier_string == "ptrdiff_t")
            return 3;
        return 0;
    case (9 << 8) | (uint8_t)'u':
        if (identifier_string == "uintmax_t")
            return 3;
        // return 0;
        // case (9 << 8) | (uint8_t)'u':
        if (identifier_string == "uintptr_t")
            return 3;
        return 0;
    case (10 << 8) | (uint8_t)'c':
        if (identifier_string == "const_cast")
            return 2;
        return 0;
    case (11 << 8) | (uint8_t)'i':
        if (identifier_string == "int_fast8_t")
            return 3;
        return 0;
    case (11 << 8) | (uint8_t)'s':
        if (identifier_string == "static_cast")
            return 2;
        return 0;
    case (12 << 8) | (uint8_t)'d':
        if (identifier_string == "dynamic_cast")
            return 2;
        return 0;
    case (12 << 8) | (uint8_t)'i':
        if (identifier_string == "int_fast16_t")
            return 3;
        // return 0;
        // case (12 << 8) | (uint8_t)'i':
        if (identifier_string == "int_fast32_t")
            return 3;
        // return 0;
        // case (12 << 8) | (uint8_t)'i':
        if (identifier_string == "int_fast64_t")
            return 3;
        // return 0;
        // case (12 << 8) | (uint8_t)'i':
        if (identifier_string == "int_least8_t")
            return 3;
        return 0;
    case (12 << 8) | (uint8_t)'s':
        if (identifier_string == "synchronized")
            return 2;
        return 0;
    case (12 << 8) | (uint8_t)'t':
        if (identifier_string == "thread_local")
            return 2;
        return 0;
    case (12 << 8) | (uint8_t)'u':
        if (identifier_string == "uint_fast8_t")
            return 3;
        return 0;
    case (13 << 8) | (uint8_t)'a':
        if (identifier_string == "atomic_cancel")
            return 2;
        // return 0;
        // case (13 << 8) | (uint8_t)'a':
        if (identifier_string == "atomic_commit")
            return 2;
        return 0;
    case (13 << 8) | (uint8_t)'i':
        if (identifier_string == "int_least16_t")
            return 3;
        // return 0;
        // case (13 << 8) | (uint8_t)'i':
        if (identifier_string == "int_least32_t")
            return 3;
        // return 0;
        // case (13 << 8) | (uint8_t)'i':
        if (identifier_string == "int_least64_t")
            return 3;
        return 0;
    case (13 << 8) | (uint8_t)'s':
        if (identifier_string == "static_assert")
            return 2;
        return 0;
    case (13 << 8) | (uint8_t)'u':
        if (identifier_string == "uint_fast16_t")
            return 3;
        // return 0;
        // case (13 << 8) | (uint8_t)'u':
        if (identifier_string == "uint_fast32_t")
            return 3;
        // return 0;
        // case (13 << 8) | (uint8_t)'u':
        if (identifier_string == "uint_fast64_t")
            return 3;
        return 0;
    case (14 << 8) | (uint8_t)'u':
        if (identifier_string == "uint_least16_t")
            return 3;
        // return 0;
        // case (14 << 8) | (uint8_t)'u':
        if (identifier_string == "uint_least32_t")
            return 3;
        // return 0;
        // case (14 << 8) | (uint8_t)'u':
        if (identifier_string == "uint_least64_t")
            return 3;
        return 0;
    case (15 << 8) | (uint8_t)'a':
        if (identifier_string == "atomic_noexcept")
            return 2;
        return 0;
    case (16 << 8) | (uint8_t)'r':
        if (identifier_string == "reinterpret_cast")
            return 2;
        return 0;

    default:
        return 0;
    }
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
        } else if (cz::is_blank(ch)) {
        } else if (ch == '`') {
            *comment_state = COMMENT_MIDDLE_OF_LINE;
            break;
        } else if (*comment_state == COMMENT_START_OF_LINE_2 &&
                   (ch == '#' || ch == '*' || ch == '-' || ch == '+')) {
            break;
        } else {
            continue_into_next_line = false;
            *comment_state = COMMENT_MIDDLE_OF_LINE;
        }
    }
}

static void continue_around_oneline_comment(Contents_Iterator* iterator) {
    for (bool continue_into_next_line = false; !iterator->at_eob(); iterator->advance()) {
        char ch = iterator->get();
        if (ch == '\n') {
            if (!continue_into_next_line) {
                break;
            }
            continue_into_next_line = false;
        } else if (ch == '\\') {
            continue_into_next_line = true;
        } else if (cz::is_blank(ch)) {
        } else {
            continue_into_next_line = false;
        }
    }
}

static void continue_inside_multiline_comment(Contents_Iterator* iterator,
                                              bool* in_multiline_doc_comment,
                                              uint64_t* comment_state) {
    int counter = 0;
    char previous = 0;
    for (; !iterator->at_eob(); iterator->advance()) {
        // If a comment is incredibly large, parse it as multiple multiple
        // tokens so we don't stall the editor.  In practice this is unlikely
        // to happen unless the user types /** at the start of a big file.
        if (++counter == 1000) {
            // Pause in the middle of the comment.
            if (*comment_state != COMMENT_TITLE) {
                *comment_state = COMMENT_MIDDLE_OF_LINE;
            }
            break;
        }

        char ch = iterator->get();
        if (previous == '*' && ch == '/') {
        end_comment:
            if (*comment_state == COMMENT_TITLE) {
                iterator->retreat();
                *comment_state = COMMENT_MIDDLE_OF_LINE;
            } else {
                iterator->advance();
                *in_multiline_doc_comment = false;
            }
            break;
        } else if (ch == '\n') {
            if (*comment_state == COMMENT_TITLE) {
                break;
            }
            *comment_state = COMMENT_START_OF_LINE_1;
        } else if (ch == '`') {
            *comment_state = COMMENT_MIDDLE_OF_LINE;
            break;
        } else if ((*comment_state == COMMENT_START_OF_LINE_1 ||
                    *comment_state == COMMENT_START_OF_LINE_2) &&
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

static void continue_around_multiline_comment(Contents_Iterator* iterator,
                                              bool* in_multiline_normal_comment) {
    // Normally we'll reach the end of the comment.
    *in_multiline_normal_comment = false;

    int counter = 0;
    char previous = 0;
    for (; !iterator->at_eob(); iterator->advance()) {
        // If a comment is incredibly large, parse it as multiple multiple
        // tokens so we don't stall the editor.  In practice this is unlikely
        // to happen unless the user types /** at the start of a big file.
        if (++counter == 1000) {
            *in_multiline_normal_comment = true;
            break;
        }

        char ch = iterator->get();
        if (previous == '*' && ch == '/') {
            iterator->advance();
            break;
        }

        previous = ch;
    }
}

bool cpp_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state_combined) {
    ZoneScoped;

    bool in_preprocessor;
    uint64_t normal_state;
    uint64_t preprocessor_state;
    uint64_t preprocessor_saved_state;
    bool in_multiline_doc_comment;
    bool in_multiline_normal_comment;
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

    token->start = iterator->position;

    if (first_char == '<' || first_char == '=' || first_char == '>') {
        Contents_Iterator mc = *iterator;
        mc.advance();
        size_t i = 0;
        for (; i < 6; ++i) {
            if (mc.at_eob()) {
                break;
            }
            if (mc.get() != first_char) {
                break;
            }
            mc.advance();
        }
        if (i == 6) {
            switch (first_char) {
            case '<':
                token->type = Token_Type::MERGE_START;
                break;
            case '=':
                token->type = Token_Type::MERGE_MIDDLE;
                break;
            case '>':
                token->type = Token_Type::MERGE_END;
                break;
            }
            *iterator = mc;
            end_of_line(iterator);
            goto done;
        }
    }

    if (in_multiline_normal_comment) {
        ZoneScopedN("block comment continuation");
        continue_around_multiline_comment(iterator, &in_multiline_normal_comment);
        token->type = Token_Type::COMMENT;
        goto done;
    }

    if (in_oneline_comment || in_multiline_doc_comment) {
        // The fact that we stopped here in the middle of a comment means we hit a special
        // character.  As of right now that is one of `, #, *, -, or +.
        switch (comment_state) {
        case COMMENT_START_OF_LINE_1:
        case COMMENT_START_OF_LINE_2:
            switch (first_char) {
            case '#':
                comment_state = COMMENT_TITLE;
                iterator->advance();
                while (!iterator->at_eob() && iterator->get() == '#') {
                    iterator->advance();
                }
                token->type = Token_Type::PUNCTUATION;
                break;
            case '*':
                // Handle */.
                if (in_multiline_doc_comment && looking_at(*iterator, "*/")) {
                    goto comment_normal;
                }

                // If this is the first * in a multiline doc comment then treat
                // it as a continuation character instead of a markdown list.
                if (in_multiline_doc_comment && comment_state == COMMENT_START_OF_LINE_1) {
                    comment_state = COMMENT_START_OF_LINE_2;
                    iterator->advance();
                    goto comment_normal;
                }

                // fallthrough
            case '-':
            case '+':
                comment_state = COMMENT_MIDDLE_OF_LINE;
                iterator->advance();
                token->type = Token_Type::PUNCTUATION;
                break;
            default:
                goto comment_normal;
            }
            goto done;

        case COMMENT_TITLE:
            if (in_oneline_comment) {
                continue_inside_oneline_comment(iterator, &in_oneline_comment, &comment_state);
            } else {
                continue_inside_multiline_comment(iterator, &in_multiline_doc_comment,
                                                  &comment_state);
            }
            token->type = Token_Type::TITLE;
            goto done;

        case COMMENT_MIDDLE_OF_LINE:
            if (first_char == '`') {
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

                token->type = Token_Type::OPEN_PAIR;
                goto done;
            } else {
            comment_normal:
                if (in_oneline_comment) {
                    continue_inside_oneline_comment(iterator, &in_oneline_comment, &comment_state);
                } else {
                    continue_inside_multiline_comment(iterator, &in_multiline_doc_comment,
                                                      &comment_state);
                }
                token->type = Token_Type::DOC_COMMENT;
                goto done;
            }

        case COMMENT_CODE_INLINE:
            if (first_char == '`') {
                iterator->advance();

            end_comment_code:
                MAKE_COMBINED_STATE(*state_combined);
                *state_combined &= ~COMMENT_STATE_TO_SAVE_MASK;
                *state_combined |=
                    (*state_combined & COMMENT_SAVED_STATE_MASK) >> COMMENT_SAVE_SHIFT;
                LOAD_COMBINED_STATE(*state_combined);

                comment_state = COMMENT_MIDDLE_OF_LINE;
                token->type = Token_Type::CLOSE_PAIR;
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
                        it.advance();
                        *iterator = it;
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

        iterator->advance();
        token->type = Token_Type::PUNCTUATION;
        goto done;
    }

    if (first_char == '"' || (first_char == '<' && in_preprocessor &&
                              preprocessor_state == PREPROCESSOR_AFTER_INCLUDE)) {
        ZoneScopedN("string");
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
                if (iterator->position + 1 < iterator->contents->len) {
                    // Only skip over the next character if we don't go out of bounds.
                    iterator->advance();

                    // Skip all spaces and then we eat newline in the outer loop.
                    while (!iterator->at_eob() && cz::is_blank(iterator->get())) {
                        iterator->advance();
                    }

                    if (iterator->at_eob()) {
                        break;
                    }
                }
            }
        }
        token->type = Token_Type::STRING;
        normal_state = IN_EXPR;
        goto done;
    }

    if (first_char == '\'') {
        ZoneScopedN("character");
        if (iterator->position + 3 >= iterator->contents->len) {
            iterator->advance_to(iterator->contents->len);
        } else {
            iterator->advance();
            if (iterator->get() == '\\') {
                iterator->advance(3);
            } else {
                iterator->advance(2);
            }
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

    if (cz::is_alpha(first_char) || first_char == '_') {
        ZoneScopedN("identifier");

        cz::String identifier_string = {};
        CZ_DEFER(identifier_string.drop(cz::heap_allocator()));

        Contents_Iterator start_iterator = *iterator;
        {
            ZoneScopedN("find end");
            identifier_string.reserve(cz::heap_allocator(), 1);
            identifier_string.push(first_char);
            char ch;
            for (iterator->advance();
                 !iterator->at_eob() && is_identifier_continuation((ch = iterator->get()));
                 iterator->advance()) {
                identifier_string.reserve(cz::heap_allocator(), 1);
                identifier_string.push(ch);
            }
        }

        if (in_preprocessor && preprocessor_state == PREPROCESSOR_START_STATEMENT) {
            ZoneScopedN("preprocessor keyword");
            token->type = Token_Type::PREPROCESSOR_KEYWORD;
            if (matches(start_iterator, iterator->position, "include")) {
                preprocessor_state = PREPROCESSOR_AFTER_INCLUDE;
                goto done_no_skip;
            } else if (matches(start_iterator, iterator->position, "define")) {
                preprocessor_state = PREPROCESSOR_AFTER_DEFINE;
                goto done_no_skip;
            } else {
                if (looking_at(start_iterator, "if")) {
                    token->type = Token_Type::PREPROCESSOR_IF;
                } else if (looking_at(start_iterator, "end")) {
                    token->type = Token_Type::PREPROCESSOR_ENDIF;
                } else if (looking_at(start_iterator, "el")) {
                    token->type = Token_Type::PREPROCESSOR_ELSE;
                }
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

        switch (look_for_keyword(identifier_string)) {
        case 1:
            // Type definition keyword.
            token->type = Token_Type::KEYWORD;
            normal_state = IN_TYPE_DEFINITION;
            goto done;

        case 2:
            // Normal keyword.
            token->type = Token_Type::KEYWORD;
            if (matches(start_iterator, iterator->position, "for")) {
                normal_state = AFTER_FOR;
            } else if (matches(start_iterator, iterator->position, "return")) {
                normal_state = IN_EXPR;
            }
            goto done;

        case 3:
            // Type keyword.
            token->type = Token_Type::TYPE;
            if (normal_state == START_OF_PARAMETER || normal_state == IN_PARAMETER_TYPE) {
                normal_state = IN_PARAMETER_TYPE;
            } else {
                normal_state = IN_VARIABLE_TYPE;
            }
            goto done;

        default:
            break;
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
            if (!cpp_next_token(&next_token_iterator, &next_token, &temp_state)) {
                // couldn't get next token
            } else if (in_preprocessor && !(temp_state & IN_PREPROCESSOR_FLAG)) {
                // next token is outside preprocessor invocation we are in
            } else {
                Contents_Iterator it = next_token_iterator;
                CZ_DEBUG_ASSERT(it.position > next_token.start);
                it.retreat_to(next_token.start);
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
                                if (!cpp_next_token(&next_token_iterator, &next_token,
                                                    &temp_state)) {
                                    // couldn't get next token
                                } else if (in_preprocessor &&
                                           !(temp_state & IN_PREPROCESSOR_FLAG)) {
                                    // next token is outside preprocessor invocation we are in
                                } else if (next_token.type == Token_Type::TYPE) {
                                    is_type = true;
                                }
                            }
                        } else if (start_ch == '&' && start_ch2 == '&') {
                            is_type = true;
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
                } else if (normal_state == START_OF_PARAMETER &&
                           next_token.end == next_token.start + 1 && start_ch == ')') {
                    normal_state = START_OF_STATEMENT;
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
            next_iterator.advance();
            *iterator = next_iterator;
            if (!iterator->at_eob() && iterator->get() == '/') {
                iterator->advance();
                in_oneline_comment = true;
                if (previous_in_oneline_comment && (comment_state == COMMENT_CODE_INLINE ||
                                                    comment_state == COMMENT_CODE_MULTILINE)) {
                    // Merge consecutive online comments where a code block extends between them.
                } else {
                    comment_state = COMMENT_START_OF_LINE_2;
                    continue_inside_oneline_comment(iterator, &in_oneline_comment, &comment_state);
                }
                token->type = Token_Type::DOC_COMMENT;
            } else {
                continue_around_oneline_comment(iterator);
                token->type = Token_Type::COMMENT;
            }
            goto done;
        }

        if (!next_iterator.at_eob() && next_iterator.get() == '*') {
            ZoneScopedN("block comment");
            next_iterator.advance();
            *iterator = next_iterator;
            if (!iterator->at_eob() && iterator->get() == '*') {
                iterator->advance();
                if (!iterator->at_eob() && iterator->get() == '/') {
                    iterator->advance();
                    token->type = Token_Type::COMMENT;
                } else {
                    in_multiline_doc_comment = true;
                    comment_state = COMMENT_START_OF_LINE_2;
                    continue_inside_multiline_comment(iterator, &in_multiline_doc_comment,
                                                      &comment_state);
                    token->type = Token_Type::DOC_COMMENT;
                }
            } else {
                continue_around_multiline_comment(iterator, &in_multiline_normal_comment);
                token->type = Token_Type::COMMENT;
            }
            goto done;
        }
    }

    if (cz::is_punct(first_char)) {
        ZoneScopedN("punctuation");
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

        if (first_char == '(' || first_char == '{' || first_char == '[') {
            token->type = Token_Type::OPEN_PAIR;
        } else if (first_char == ')' || first_char == '}' || first_char == ']') {
            token->type = Token_Type::CLOSE_PAIR;
        } else {
            token->type = Token_Type::PUNCTUATION;
        }

        if (first_char == ';' || first_char == '{' || first_char == '}' ||
            (first_char == ':' && second_char != ':')) {
            normal_state = START_OF_STATEMENT;
        } else if (normal_state == IN_VARIABLE_TYPE && first_char == ':' && second_char == ':') {
            normal_state = START_OF_STATEMENT;
        } else if (normal_state == IN_PARAMETER_TYPE && first_char == ':' && second_char == ':') {
            normal_state = START_OF_PARAMETER;
        } else if (normal_state == START_OF_STATEMENT || first_char == '.') {
            normal_state = IN_EXPR;
        } else if (normal_state == AFTER_FOR && first_char == '(') {
            normal_state = START_OF_STATEMENT;
        } else if (normal_state == IN_VARIABLE_TYPE && first_char == ')') {
            normal_state = IN_EXPR;  // End of cast expression.
        } else if (normal_state == AFTER_VARIABLE_DECLARATION && first_char == '(') {
            normal_state = START_OF_PARAMETER;
        } else if (normal_state == AFTER_VARIABLE_DECLARATION && first_char == '=') {
            normal_state = IN_EXPR;
        } else if ((normal_state == AFTER_PARAMETER_DECLARATION ||
                    normal_state == IN_PARAMETER_TYPE) &&
                   first_char == ',') {
            normal_state = START_OF_PARAMETER;
        } else if (normal_state == START_OF_PARAMETER && token->type == Token_Type::PUNCTUATION) {
            normal_state = IN_EXPR;  // Misrecognized constructor call as function declaration.
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

    if (cz::is_digit(first_char)) {
        iterator->advance();
        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (!cz::is_alnum(ch) && ch != '.') {
                break;
            }
            iterator->advance();
        }
        token->type = Token_Type::NUMBER;
        normal_state = IN_EXPR;
        goto done;
    }

    iterator->advance();
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

    token->end = iterator->position;
    return true;
}

}
}
