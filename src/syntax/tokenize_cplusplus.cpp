#include "tokenize_cplusplus.hpp"

#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/string.hpp>
#include <tracy/Tracy.hpp>
#include "core/contents.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

///////////////////////////////////////////////////////////////////////////////
// Type definitions
///////////////////////////////////////////////////////////////////////////////

namespace {

/// Doc comment state used for highlighting markdown and embedded code blocks.  Markdown
/// characters can only happen at the start of the line (SOL) and thus middle of the line
/// (MOL) states bypass Markdown detection.  Inside/outside refers to whether the cursor is
/// inside or outside of a code block ie `|` for single line or ```\n|\n``` for multi line.
enum {
    COMMENT_NULL = 0,
    COMMENT_LINE_INSIDE_INLINE = 1,                    /// '/// `|x`'
    COMMENT_LINE_INSIDE_MULTI_LINE = 2,                /// '/// ```|x```'
    COMMENT_LINE_RESUME_INSIDE = 3,                    /// '/// `x|`|' (either)
    COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_SOL = 4,  /// '/// ```\n|/// xyz\n/// ```'
    COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_MOL = 5,  /// '/// ```\n///| xyz|\n/// ```' (either)
    COMMENT_LINE_RESUME_INSIDE_STRING_SOL = 6,         /// '/// ```\n|/// xyz\n/// ```'
    COMMENT_LINE_RESUME_INSIDE_STRING_MOL = 7,         /// '/// ```\n///| xyz|\n/// ```' (either)
    COMMENT_LINE_RESUME_OUTSIDE_SOL = 8,               /// '/// |'
    COMMENT_LINE_RESUME_OUTSIDE_MOL = 9,               /// '/// ``|'
    COMMENT_LINE_RESUME_OUTSIDE_HEADER = 10,           /// '/// #| Header'
    COMMENT_LINE_OUTSIDE_MULTI_LINE = 11,              /// '/// ```|\n///```'
    COMMENT_BLOCK_INSIDE_INLINE = 12,                  /// '/** `|x` */'
    COMMENT_BLOCK_INSIDE_MULTI_LINE = 13,              /// '/** ```|x``` */'
    COMMENT_BLOCK_RESUME_INSIDE = 14,                  /// '/** `x|` */'
    COMMENT_BLOCK_RESUME_OUTSIDE_SOL1 = 15,  /// '/**\n|* - x */' (immediately after newline)
    COMMENT_BLOCK_RESUME_OUTSIDE_SOL2 =
        16,  /// '/**| - x */' or '/**\n*| - x */' (after newline and * continuation)
    COMMENT_BLOCK_RESUME_OUTSIDE_MOL = 17,     /// '/** x |`y` */'
    COMMENT_BLOCK_RESUME_OUTSIDE_HEADER = 18,  /// '/** #| Header */'
    COMMENT_BLOCK_OUTSIDE_MULTI_LINE = 19,     /// '/** ```|\n``` */'
    COMMENT_BLOCK_NORMAL = 20,                 /// '/* ...|... */' (to prevent blocking)
};

enum {
    PREPROCESSOR_NULL = 0,
    PREPROCESSOR_AT_KEYWORD = 1,
    PREPROCESSOR_INSIDE = 2,
    PREPROCESSOR_AFTER_INCLUDE = 3,
    PREPROCESSOR_AFTER_DEFINE = 4,
    PREPROCESSOR_AFTER_DEFINE_PAREN = 5,
};

enum {
    SYNTAX_AT_STMT = 0,
    SYNTAX_IN_EXPR = 1,
    SYNTAX_AT_TYPE = 2,
    SYNTAX_AFTER_TYPE = 3,
    SYNTAX_AFTER_DECL = 4,
    SYNTAX_AT_RAW_STRING_LITERAL = 5,
    SYNTAX_AFTER_KEYWORD_OPERATOR = 6,
};

struct State {
    uint64_t comment : 5;
    uint64_t comment_saved_preprocessor : 3;
    uint64_t comment_saved_preprocessor_saved_syntax : 3;
    uint64_t comment_saved_syntax : 3;
    uint64_t preprocessor : 3;
    uint64_t preprocessor_saved_syntax : 3;
    uint64_t syntax : 3;
    uint64_t padding : 41;
};
static_assert(sizeof(State) == sizeof(uint64_t), "Must be able to cast between the two");
}

///////////////////////////////////////////////////////////////////////////////
// cpp_next_token
///////////////////////////////////////////////////////////////////////////////

static bool handle_comment(Contents_Iterator* iterator, Token* token, State* state);

bool cpp_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state_) {
    ZoneScoped;
    State* state = (State*)state_;
    return handle_comment(iterator, token, state);
}

///////////////////////////////////////////////////////////////////////////////
// handle_comment
///////////////////////////////////////////////////////////////////////////////

static bool handle_preprocessor(Contents_Iterator* iterator, Token* token, State* state);
static bool resume_line_comment_doc(Contents_Iterator* iterator, Token* token, State* state);
static bool resume_line_comment_header(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_line_comment_outside_multi_line(Contents_Iterator* iterator,
                                                   Token* token,
                                                   State* state);
static bool resume_block_comment_doc(Contents_Iterator* iterator, Token* token, State* state);
static bool resume_block_comment_header(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_block_comment_outside_multi_line(Contents_Iterator* iterator,
                                                    Token* token,
                                                    State* state);
static bool resume_block_comment_normal(Contents_Iterator* iterator, Token* token, State* state);
static void handle_string(Contents_Iterator* iterator, Token* token, State* state, bool at_start);

static bool handle_comment(Contents_Iterator* iterator, Token* token, State* state) {
    switch (state->comment) {
    case COMMENT_NULL:
    case COMMENT_LINE_INSIDE_INLINE:
    case COMMENT_LINE_INSIDE_MULTI_LINE:
    case COMMENT_BLOCK_INSIDE_INLINE:
    case COMMENT_BLOCK_INSIDE_MULTI_LINE:
        return handle_preprocessor(iterator, token, state);
    case COMMENT_LINE_RESUME_INSIDE:
    case COMMENT_LINE_RESUME_OUTSIDE_SOL:
    case COMMENT_LINE_RESUME_OUTSIDE_MOL:
        return resume_line_comment_doc(iterator, token, state);
    case COMMENT_LINE_RESUME_OUTSIDE_HEADER:
        return resume_line_comment_header(iterator, token, state);
    case COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_SOL:
    case COMMENT_LINE_RESUME_INSIDE_STRING_SOL:
    case COMMENT_LINE_OUTSIDE_MULTI_LINE:
        return handle_line_comment_outside_multi_line(iterator, token, state);
    case COMMENT_BLOCK_RESUME_INSIDE:
    case COMMENT_BLOCK_RESUME_OUTSIDE_SOL1:
    case COMMENT_BLOCK_RESUME_OUTSIDE_SOL2:
    case COMMENT_BLOCK_RESUME_OUTSIDE_MOL:
        return resume_block_comment_doc(iterator, token, state);
    case COMMENT_BLOCK_RESUME_OUTSIDE_HEADER:
        return resume_block_comment_header(iterator, token, state);
    case COMMENT_BLOCK_OUTSIDE_MULTI_LINE:
        return handle_block_comment_outside_multi_line(iterator, token, state);
    case COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_MOL:
    case COMMENT_BLOCK_NORMAL:
        return resume_block_comment_normal(iterator, token, state);
    case COMMENT_LINE_RESUME_INSIDE_STRING_MOL:
        handle_string(iterator, token, state, /*at_start=*/false);
        return true;
    default:
        return false;
    }
}

static void comment_push(State* state, uint8_t comment) {
    state->comment = comment;
    state->comment_saved_preprocessor = state->preprocessor;
    state->comment_saved_preprocessor_saved_syntax = state->preprocessor_saved_syntax;
    state->comment_saved_syntax = state->syntax;
    state->preprocessor = 0;
    state->preprocessor_saved_syntax = 0;
    state->syntax = 0;
}
static void comment_pop(State* state) {
    state->comment = COMMENT_NULL;
    state->preprocessor = state->comment_saved_preprocessor;
    state->preprocessor_saved_syntax = state->preprocessor_saved_syntax;
    state->syntax = state->comment_saved_syntax;
    state->comment_saved_preprocessor = 0;
    state->preprocessor_saved_syntax = 0;
    state->comment_saved_syntax = 0;
}

///////////////////////////////////////////////////////////////////////////////
// handle_preprocessor
///////////////////////////////////////////////////////////////////////////////

static bool handle_syntax(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_preprocessor_keyword(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_preprocessor_include(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_preprocessor_define(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_preprocessor_define_paren(Contents_Iterator* iterator,
                                             Token* token,
                                             State* state);

static bool handle_preprocessor(Contents_Iterator* iterator, Token* token, State* state) {
    switch (state->preprocessor) {
    case PREPROCESSOR_NULL:
    case PREPROCESSOR_INSIDE:
        return handle_syntax(iterator, token, state);
    case PREPROCESSOR_AT_KEYWORD:
        return handle_preprocessor_keyword(iterator, token, state);
    case PREPROCESSOR_AFTER_INCLUDE:
        return handle_preprocessor_include(iterator, token, state);
    case PREPROCESSOR_AFTER_DEFINE:
        return handle_preprocessor_define(iterator, token, state);
    case PREPROCESSOR_AFTER_DEFINE_PAREN:
        return handle_preprocessor_define_paren(iterator, token, state);
    default:
        return false;
    }
}

static void preprocessor_push(State* state, uint8_t preprocessor) {
    state->preprocessor = preprocessor;
    state->preprocessor_saved_syntax = state->syntax;
    state->syntax = 0;
}
static void preprocessor_pop(State* state) {
    state->preprocessor = PREPROCESSOR_NULL;
    state->syntax = state->preprocessor_saved_syntax;
    state->preprocessor_saved_syntax = 0;
}

///////////////////////////////////////////////////////////////////////////////
// handle_syntax
///////////////////////////////////////////////////////////////////////////////

static void handle_identifier(Contents_Iterator* iterator,
                              char first_ch,
                              Token* token,
                              State* state);

static void handle_number(Contents_Iterator* iterator, Token* token, State* state);

static void punctuation_simple(Contents_Iterator* iterator, Token* token, State* state);
static void punctuation_set(Contents_Iterator* iterator, Token* token, State* state);
static void punctuation_less_greater(Contents_Iterator* iterator,
                                     char first_ch,
                                     Token* token,
                                     State* state);
static void punctuation_double_or_set(Contents_Iterator* iterator,
                                      char first_ch,
                                      Token* token,
                                      State* state);

static void handle_line_comment(Contents_Iterator* iterator, Token* token, State* state);
static void handle_block_comment(Contents_Iterator* iterator, Token* token, State* state);
static bool at_end_of_block_comment(Contents_Iterator iterator, State state);

static void handle_char(Contents_Iterator* iterator, Token* token, State* state);
static void eat_raw_string_literal(Contents_Iterator* iterator);

static bool handle_syntax(Contents_Iterator* iterator, Token* token, State* state) {
retry:
    if (iterator->at_eob())
        return false;

    char first_ch = iterator->get();
    switch (first_ch) {
    case CZ_ALPHA_CASES:
    case '_':
    case '$':
        handle_identifier(iterator, first_ch, token, state);
        return true;

    case CZ_DIGIT_CASES:
        handle_number(iterator, token, state);
        return true;

    case '{':
        state->syntax = SYNTAX_AT_STMT;
        goto open_pair;
    case '[':
        state->syntax = SYNTAX_IN_EXPR;
        // fallthrough
    case '(':
    open_pair:
        token->type = Token_Type::OPEN_PAIR;
        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
        return true;
    case '}':
        state->syntax = SYNTAX_AT_STMT;
        goto close_pair;
    case ')':
    case ']':
        state->syntax = SYNTAX_IN_EXPR;
    close_pair:
        token->type = Token_Type::CLOSE_PAIR;
        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
        return true;

    case '!':
    case '~':
    case '?':
        punctuation_simple(iterator, token, state);
        state->syntax = SYNTAX_IN_EXPR;
        return true;

    case ',':
        punctuation_simple(iterator, token, state);
        // Don't change the state so parameters are all parsed the same way.
        return true;

    case ';':
        punctuation_simple(iterator, token, state);
        state->syntax = SYNTAX_AT_STMT;
        return true;

    case '#':
        if (state->preprocessor != PREPROCESSOR_INSIDE) {
            preprocessor_push(state, PREPROCESSOR_AT_KEYWORD);
            state->syntax = SYNTAX_AT_STMT;
        }
        punctuation_simple(iterator, token, state);
        return true;

    case ':':
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        if (!iterator->at_eob() && iterator->get() == first_ch)
            iterator->advance();
        else
            state->syntax = SYNTAX_AT_STMT;
        token->end = iterator->position;
        return true;

    case '*':
        // Parse '*/' inside an inline code block inside a block doc comment as end of comment.
        if (at_end_of_block_comment(*iterator, *state)) {
            state->comment = COMMENT_BLOCK_RESUME_INSIDE;
            return resume_block_comment_doc(iterator, token, state);
        }
        // fallthrough

    case '=':
    case '^':
    case '%':
        punctuation_set(iterator, token, state);
        return true;

    case '<':
    case '>':
        punctuation_less_greater(iterator, first_ch, token, state);
        return true;

    case '+':
    case '|':
    case '&':
        punctuation_double_or_set(iterator, first_ch, token, state);
        return true;

    case '.': {
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        if (looking_at(*iterator, '*')) /* .* */ {
            // Parse '.*/' inside an inline code block inside
            // a block doc comment as '.' then end of comment.
            if (!at_end_of_block_comment(*iterator, *state)) {
                iterator->advance();
            }
        }
        if (looking_at(*iterator, ".."))  // ...
            iterator->advance(2);
        token->end = iterator->position;
        state->syntax = SYNTAX_IN_EXPR;
        return true;
    }

    case '-': {
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        if (!iterator->at_eob()) {
            char ch = iterator->get();
            if (ch == first_ch || ch == '=')  // -- and -=
                iterator->advance();
            if (ch == '>') {  // ->
                iterator->advance();
                if (looking_at(*iterator, '*')) /* ->* */ {
                    // Parse '->*/' inside an inline code block inside
                    // a block doc comment as '->' then end of comment.
                    if (!at_end_of_block_comment(*iterator, *state)) {
                        iterator->advance();
                    }
                }
            }
        }
        token->end = iterator->position;
        state->syntax = SYNTAX_IN_EXPR;
        return true;
    }

    case '/': {
        bool is_punctuation = true;
        token->start = iterator->position;
        iterator->advance();
        if (!iterator->at_eob()) {
            char ch = iterator->get();
            if (ch == '=') {  // /=
                iterator->advance();
            } else if (ch == '/') {  // //
                is_punctuation = false;
                iterator->advance();
                handle_line_comment(iterator, token, state);
            } else if (ch == '*') {  // /*
                is_punctuation = false;
                iterator->advance();
                handle_block_comment(iterator, token, state);
            }
        }
        if (is_punctuation) {
            token->type = Token_Type::PUNCTUATION;
            token->end = iterator->position;
            state->syntax = SYNTAX_IN_EXPR;
        }
        return true;
    }

    case '\'':
        handle_char(iterator, token, state);
        return true;
    case '"':
        handle_string(iterator, token, state, /*at_start=*/true);
        return true;

    case '`':
        if (state->comment == COMMENT_LINE_INSIDE_INLINE ||
            state->comment == COMMENT_LINE_INSIDE_MULTI_LINE) {
            state->comment = COMMENT_LINE_RESUME_INSIDE;
            return resume_line_comment_doc(iterator, token, state);
        }
        if (state->comment == COMMENT_BLOCK_INSIDE_INLINE ||
            state->comment == COMMENT_BLOCK_INSIDE_MULTI_LINE) {
            state->comment = COMMENT_BLOCK_RESUME_INSIDE;
            return resume_block_comment_doc(iterator, token, state);
        }
        iterator->advance();
        goto retry;

    case CZ_SPACE_LINE_CASES:
        if (state->preprocessor == PREPROCESSOR_INSIDE) {
            preprocessor_pop(state);
        }
        if (state->comment == COMMENT_LINE_INSIDE_INLINE ||
            state->comment == COMMENT_LINE_INSIDE_MULTI_LINE) {
            state->comment = COMMENT_LINE_RESUME_INSIDE;
            return resume_line_comment_doc(iterator, token, state);
        }
        if (state->comment == COMMENT_BLOCK_INSIDE_INLINE) {
            state->comment = COMMENT_BLOCK_RESUME_INSIDE;
            return resume_block_comment_doc(iterator, token, state);
        }
        if (state->comment == COMMENT_BLOCK_INSIDE_MULTI_LINE) {
            state->comment = COMMENT_BLOCK_OUTSIDE_MULTI_LINE;
            return handle_block_comment_outside_multi_line(iterator, token, state);
        }
        // fallthrough

    default:
        iterator->advance();
        goto retry;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Identifier
///////////////////////////////////////////////////////////////////////////////

namespace {
enum Keyword_Type {
    NOT_KEYWORD = 0,
    KEYWORD_TYPE_DECLARATION = 1,    /// e.g. 'struct'
    KEYWORD_GENERAL = 2,             /// e.g. 'if'
    KEYWORD_TYPE = 3,                /// e.g. 'char'
    KEYWORD_STATEMENT_PREFIX = 4,    /// e.g. 'static'
    KEYWORD_START_BLOCK = 5,         /// e.g. 'BOOST_AUTO_TEST_SUITE'
    KEYWORD_END_BLOCK = 6,           /// e.g. 'BOOST_AUTO_TEST_SUITE_END'
    KEYWORD_OPERATOR = 7,            /// 'operator'
    KEYWORD_RAW_STRING_LITERAL = 8,  /// e.g. 'R'
};
}

static bool is_type(Contents_Iterator iterator, State* state);
static Keyword_Type look_for_keyword(Contents_Iterator start, uint64_t len, char first_ch);

static void handle_identifier(Contents_Iterator* iterator,
                              char first_ch,
                              Token* token,
                              State* state) {
    ZoneScoped;

    Contents_Iterator start = *iterator;
    token->start = iterator->position;
    iterator->advance();

    while (!iterator->at_eob()) {
        char ch = iterator->get();
        if (!cz::is_alnum(ch) && ch != '_' && ch != '$')
            break;
        iterator->advance();
    }

    token->end = iterator->position;

    switch (look_for_keyword(start, iterator->position - start.position, first_ch)) {
    case KEYWORD_TYPE_DECLARATION:
        token->type = Token_Type::KEYWORD;
        state->syntax = SYNTAX_AT_TYPE;
        break;
    case KEYWORD_GENERAL:
        token->type = Token_Type::KEYWORD;
        state->syntax = SYNTAX_IN_EXPR;
        break;
    case KEYWORD_TYPE:
        token->type = Token_Type::TYPE;
        state->syntax = SYNTAX_AFTER_TYPE;
        break;
    case KEYWORD_STATEMENT_PREFIX:
        token->type = Token_Type::KEYWORD;
        state->syntax = SYNTAX_AT_STMT;
        break;
    case KEYWORD_START_BLOCK:
        token->type = Token_Type::PREPROCESSOR_IF;
        state->syntax = SYNTAX_AFTER_DECL;
        break;
    case KEYWORD_END_BLOCK:
        token->type = Token_Type::PREPROCESSOR_ENDIF;
        state->syntax = SYNTAX_AFTER_DECL;
        break;
    case KEYWORD_OPERATOR:
        token->type = Token_Type::KEYWORD;
        state->syntax = SYNTAX_AFTER_KEYWORD_OPERATOR;
        break;
    case KEYWORD_RAW_STRING_LITERAL:
        if (looking_at(*iterator, '"')) {
            token->type = Token_Type::KEYWORD;
            state->syntax = SYNTAX_AT_RAW_STRING_LITERAL;
            break;
        }

        // fallthrough
    default:
        // Not a keyword.
        if (is_type(*iterator, state)) {
            token->type = Token_Type::TYPE;
            state->syntax = SYNTAX_AFTER_TYPE;
        } else {
            token->type = Token_Type::IDENTIFIER;
            if (state->syntax == SYNTAX_AFTER_TYPE) {
                state->syntax = SYNTAX_AFTER_DECL;
            } else {
                // state->syntax = SYNTAX_IN_EXPR;
                state->syntax = SYNTAX_AFTER_DECL;
            }
        }
        break;
    }
}

static bool is_type(Contents_Iterator iterator, State* state) {
    if (state->syntax == SYNTAX_AT_TYPE)
        return true;
    if (state->syntax == SYNTAX_IN_EXPR || state->syntax == SYNTAX_AFTER_TYPE ||
        state->syntax == SYNTAX_AFTER_KEYWORD_OPERATOR)
        return false;

    while (1) {
        if (iterator.at_eob())
            return false;

        switch (iterator.get()) {
        case CZ_SPACE_LINE_CASES:
            // Handle this case parsing x as a type:
            // ```
            // #define x
            // y
            // ```
            if (state->preprocessor == PREPROCESSOR_NULL)
                break;
            else
                return false;

        case CZ_BLANK_CASES:
        case '*':
        case '&':
        case '\\':  // Assume \\\n
            break;

        case CZ_ALPHA_CASES:
        case '_':
            return true;

        default:
            return false;
        }

        iterator.advance();
    }

    return false;
}

static Keyword_Type look_for_keyword(Contents_Iterator start, uint64_t len, char first_ch) {
    switch ((len << 8) | (uint8_t)first_ch) {
    case (1 << 8) | (uint8_t)'R':
    case (1 << 8) | (uint8_t)'L':
    case (1 << 8) | (uint8_t)'u':
    case (1 << 8) | (uint8_t)'U':
        return KEYWORD_RAW_STRING_LITERAL;

    case (2 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "u8"))
            return KEYWORD_RAW_STRING_LITERAL;
        // fallthrough
    case (2 << 8) | (uint8_t)'L':
    case (2 << 8) | (uint8_t)'U':
        start.advance();
        if (start.get() == 'R')
            return KEYWORD_RAW_STRING_LITERAL;
        return NOT_KEYWORD;

    case (2 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "do"))
            return KEYWORD_STATEMENT_PREFIX;
        return NOT_KEYWORD;
    case (2 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "if"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (2 << 8) | (uint8_t)'o':
        if (looking_at_no_bounds_check(start, "or"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;

    case (3 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "u8R"))
            return KEYWORD_RAW_STRING_LITERAL;
        return NOT_KEYWORD;

    case (3 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "and"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "asm"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (3 << 8) | (uint8_t)'f':
        if (looking_at_no_bounds_check(start, "for"))
            return KEYWORD_STATEMENT_PREFIX;
        return NOT_KEYWORD;
    case (3 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (3 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "new")) {
            // Treat as a type declaration keyword because the next token will be a type.
            return KEYWORD_TYPE_DECLARATION;
        }
        if (looking_at_no_bounds_check(start, "not"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (3 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "try"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (3 << 8) | (uint8_t)'x':
        if (looking_at_no_bounds_check(start, "xor"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (4 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "auto"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (4 << 8) | (uint8_t)'b':
        if (looking_at_no_bounds_check(start, "bool"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (4 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "case"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "char"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (4 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "else"))
            return KEYWORD_STATEMENT_PREFIX;
        if (looking_at_no_bounds_check(start, "enum"))
            return KEYWORD_TYPE_DECLARATION;
        return NOT_KEYWORD;
    case (4 << 8) | (uint8_t)'g':
        if (looking_at_no_bounds_check(start, "goto"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (4 << 8) | (uint8_t)'l':
        if (looking_at_no_bounds_check(start, "long"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (4 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "this"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "true"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (4 << 8) | (uint8_t)'v':
        if (looking_at_no_bounds_check(start, "void"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (5 << 8) | (uint8_t)'b':
        if (looking_at_no_bounds_check(start, "bitor"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "break"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (5 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "catch"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "class"))
            return KEYWORD_TYPE_DECLARATION;
        if (looking_at_no_bounds_check(start, "compl"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "const"))
            return KEYWORD_STATEMENT_PREFIX;
        return NOT_KEYWORD;
    case (5 << 8) | (uint8_t)'f':
        if (looking_at_no_bounds_check(start, "false"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "float"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (5 << 8) | (uint8_t)'o':
        if (looking_at_no_bounds_check(start, "or_eq"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (5 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "short"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (5 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "throw"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (5 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "union"))
            return KEYWORD_TYPE_DECLARATION;
        if (looking_at_no_bounds_check(start, "using"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (5 << 8) | (uint8_t)'w':
        if (looking_at_no_bounds_check(start, "while"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "and_eq"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'b':
        if (looking_at_no_bounds_check(start, "bitand"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "delete"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "double"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "export"))
            return KEYWORD_STATEMENT_PREFIX;
        if (looking_at_no_bounds_check(start, "extern"))
            return KEYWORD_STATEMENT_PREFIX;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'f':
        if (looking_at_no_bounds_check(start, "friend"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "inline"))
            return KEYWORD_STATEMENT_PREFIX;
        if (looking_at_no_bounds_check(start, "int8_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "not_eq"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'p':
        if (looking_at_no_bounds_check(start, "public"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'r':
        if (looking_at_no_bounds_check(start, "return"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "signed"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "size_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "sizeof"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "static"))
            return KEYWORD_STATEMENT_PREFIX;
        if (looking_at_no_bounds_check(start, "struct"))
            return KEYWORD_TYPE_DECLARATION;
        if (looking_at_no_bounds_check(start, "switch"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "typeid"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (6 << 8) | (uint8_t)'x':
        if (looking_at_no_bounds_check(start, "xor_eq"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'_':
        if (looking_at_no_bounds_check(start, "_Pragma"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "alignas"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "alignof"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "char8_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "concept"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "default"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int16_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "int32_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "int64_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'m':
        if (looking_at_no_bounds_check(start, "mutable"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "nullptr"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'p':
        if (looking_at_no_bounds_check(start, "private"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "typedef"))
            return KEYWORD_STATEMENT_PREFIX;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint8_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'v':
        if (looking_at_no_bounds_check(start, "virtual"))
            return KEYWORD_STATEMENT_PREFIX;
        return NOT_KEYWORD;
    case (7 << 8) | (uint8_t)'w':
        if (looking_at_no_bounds_check(start, "wchar_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "char16_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "char32_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "co_await"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "co_yield"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "continue"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "decltype"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "explicit"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "intmax_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "intptr_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "noexcept"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'o':
        if (looking_at_no_bounds_check(start, "operator"))
            return KEYWORD_OPERATOR;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'r':
        if (looking_at_no_bounds_check(start, "reflexpr"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "register"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "requires"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "template"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "typename"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint16_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "uint32_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "uint64_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "unsigned"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (8 << 8) | (uint8_t)'v':
        if (looking_at_no_bounds_check(start, "volatile"))
            return KEYWORD_STATEMENT_PREFIX;
        return NOT_KEYWORD;
    case (9 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "co_return"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "consteval"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "constexpr"))
            return KEYWORD_STATEMENT_PREFIX;
        if (looking_at_no_bounds_check(start, "constinit"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (9 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "namespace"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (9 << 8) | (uint8_t)'p':
        if (looking_at_no_bounds_check(start, "protected"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "ptrdiff_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (9 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uintmax_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "uintptr_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (10 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "const_cast"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (11 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_fast8_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (11 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "static_cast"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (12 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "dynamic_cast"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (12 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_fast16_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "int_fast32_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "int_fast64_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "int_least8_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (12 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "synchronized"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (12 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "thread_local"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (12 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_fast8_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (13 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "atomic_cancel"))
            return KEYWORD_GENERAL;
        if (looking_at_no_bounds_check(start, "atomic_commit"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (13 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_least16_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "int_least32_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "int_least64_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (13 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "static_assert"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (13 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_fast16_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "uint_fast32_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "uint_fast64_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (14 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_least16_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "uint_least32_t"))
            return KEYWORD_TYPE;
        if (looking_at_no_bounds_check(start, "uint_least64_t"))
            return KEYWORD_TYPE;
        return NOT_KEYWORD;
    case (15 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "atomic_noexcept"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;
    case (16 << 8) | (uint8_t)'r':
        if (looking_at_no_bounds_check(start, "reinterpret_cast"))
            return KEYWORD_GENERAL;
        return NOT_KEYWORD;

    case (21 << 8) | (uint8_t)'B':
        if (looking_at_no_bounds_check(start, "BOOST_AUTO_TEST_SUITE"))
            return KEYWORD_START_BLOCK;
        return NOT_KEYWORD;
    case (24 << 8) | (uint8_t)'B':
        if (looking_at_no_bounds_check(start, "BOOST_FIXTURE_TEST_SUITE"))
            return KEYWORD_START_BLOCK;
        return NOT_KEYWORD;
    case (25 << 8) | (uint8_t)'B':
        if (looking_at_no_bounds_check(start, "BOOST_AUTO_TEST_SUITE_END"))
            return KEYWORD_END_BLOCK;
        return NOT_KEYWORD;

    default:
        return NOT_KEYWORD;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Number
///////////////////////////////////////////////////////////////////////////////

static void handle_number(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::NUMBER;
    token->start = iterator->position;
    iterator->advance();

    while (!iterator->at_eob()) {
        char ch = iterator->get();
        if (!cz::is_alnum(ch) && ch != '_' && ch != '\'')
            break;
        iterator->advance();
    }

    token->end = iterator->position;
}

///////////////////////////////////////////////////////////////////////////////
// Punctuation
///////////////////////////////////////////////////////////////////////////////

static void punctuation_simple(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::PUNCTUATION;
    token->start = iterator->position;
    iterator->advance();
    token->end = iterator->position;
}

static void punctuation_set(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::PUNCTUATION;
    token->start = iterator->position;
    iterator->advance();
    if (!iterator->at_eob() && iterator->get() == '=')
        iterator->advance();
    token->end = iterator->position;
    state->syntax = SYNTAX_IN_EXPR;
}

static void punctuation_less_greater(Contents_Iterator* iterator,
                                     char first_ch,
                                     Token* token,
                                     State* state) {
    char before = '\0';
    if (!iterator->at_bob()) {
        Contents_Iterator it_before = *iterator;
        it_before.retreat();
        before = it_before.get();
    }

    token->start = iterator->position;
    token->type = Token_Type::PUNCTUATION;
    iterator->advance();
    char after = '\0';
    bool look_for_templates = false;
    if (!iterator->at_eob()) {
        after = iterator->get();
        if (after == first_ch) {
            iterator->advance();  // << or >>
            if (looking_at(*iterator, '=')) {
                iterator->advance();  // <<= or >>=
            } else if (first_ch == '>' && !cz::is_space(before) &&
                       state->syntax != SYNTAX_AFTER_KEYWORD_OPERATOR) {
                // Appears to be closing multiple templates.
                iterator->retreat();
                look_for_templates = true;
            }
        } else if (after == '=') {
            iterator->advance();  // <= or >=
            if (first_ch == '<' && looking_at(*iterator, '>')) {
                iterator->advance();  // <=>
            }
        } else {
            look_for_templates = true;
        }
    } else {
        look_for_templates = true;
    }

    if (look_for_templates && state->syntax != SYNTAX_AFTER_KEYWORD_OPERATOR) {
        if ((cz::is_space(before) && !cz::is_space(after)) || !cz::is_space(before)) {
            if (first_ch == '<')
                token->type = Token_Type::OPEN_PAIR;
            else
                token->type = Token_Type::CLOSE_PAIR;
        }
    }

    token->end = iterator->position;
    state->syntax = SYNTAX_IN_EXPR;
}

static void punctuation_double_or_set(Contents_Iterator* iterator,
                                      char first_ch,
                                      Token* token,
                                      State* state) {
    token->type = Token_Type::PUNCTUATION;
    token->start = iterator->position;
    iterator->advance();
    if (!iterator->at_eob()) {
        char ch = iterator->get();
        if (ch == first_ch || ch == '=')
            iterator->advance();
    }
    token->end = iterator->position;
    state->syntax = SYNTAX_IN_EXPR;
}

///////////////////////////////////////////////////////////////////////////////
// Line comment
///////////////////////////////////////////////////////////////////////////////

static void handle_line_comment_normal(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_line_comment_doc(Contents_Iterator* iterator, Token* token, State* state);
static void handle_line_comment_doc_tilde(Contents_Iterator* iterator, Token* token, State* state);

static void handle_line_comment(Contents_Iterator* iterator, Token* token, State* state) {
    if (state->comment == COMMENT_NULL && !iterator->at_eob()) {
        char ch = iterator->get();
        if (ch == '!' || ch == '/') {
            iterator->advance();
            state->comment = COMMENT_LINE_RESUME_OUTSIDE_SOL;
            handle_line_comment_doc(iterator, token, state);
            return;
        }
    }
    handle_line_comment_normal(iterator, token, state);
}

static void handle_line_comment_normal(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::COMMENT;
    if (state->comment == COMMENT_NULL) {
        while (!iterator->at_eob()) {
            end_of_line(iterator);

            Contents_Iterator test = *iterator;
            uint64_t backslashes = 0;
            while (test.position > 0) {
                test.retreat();
                if (test.get() == '\\')
                    ++backslashes;
                else
                    break;
            }
            if (backslashes % 2 == 0)
                break;

            forward_char(iterator);
        }
    } else {
        // Find the first '\n' or '`'.
        bool look_for_end = (state->comment == COMMENT_BLOCK_INSIDE_INLINE ||
                             state->comment == COMMENT_BLOCK_INSIDE_MULTI_LINE);
        bool multi_line = (state->comment == COMMENT_LINE_INSIDE_MULTI_LINE ||
                           state->comment == COMMENT_BLOCK_INSIDE_MULTI_LINE);
        while (1) {
            // Find the first '\n', '*/'?, or '`'/'```'.
            uint64_t min = -1;
            Contents_Iterator temp = *iterator;
            while (find_bucket(&temp, '\n')) {
                // Check there isn't an unescaped backslash before us.
                // If there is then this is '\\\n' which is deleted.
                Contents_Iterator test = temp;
                uint64_t backslashes = 0;
                while (test.position > 0) {
                    test.retreat();
                    if (test.get() == '\\')
                        ++backslashes;
                    else
                        break;
                }

                if (backslashes % 2 == 0) {
                    if (min > temp.position)
                        min = temp.position;
                    break;
                }

                temp.advance();
                if (temp.index == 0)
                    break;  // Went into next bucket so stop.
            }

            temp = *iterator;
            if (multi_line ? find_bucket(&temp, "```") : find_bucket(&temp, '`')) {
                if (min > temp.position)
                    min = temp.position;
            }

            bool found_end = false;
            temp = *iterator;
            if (look_for_end && find_bucket(&temp, "*/")) {
                temp.advance(2);
                if (min > temp.position) {
                    min = temp.position;
                    found_end = true;
                }
            }

            if (min < (uint64_t)-1) {
                iterator->advance_to(min);

                // '/** `/// */' parse '*/' as end of outer comment.
                if (found_end)
                    comment_pop(state);
                break;
            }
        }
    }
    token->end = iterator->position;
}

static bool resume_line_comment_doc(Contents_Iterator* iterator, Token* token, State* state) {
    token->start = iterator->position;
    return handle_line_comment_doc(iterator, token, state);
}

// At end of line inside '/// ```' block.
static bool handle_line_comment_outside_multi_line(Contents_Iterator* iterator,
                                                   Token* token,
                                                   State* state) {
    if (iterator->at_eob())
        return false;
    iterator->advance();

    while (1) {
        if (iterator->at_eob())
            return false;

        switch (iterator->get()) {
        case '/': {
            Contents_Iterator test = *iterator;
            test.advance();
            if (test.position + 2 <= test.contents->len && test.get() == '/') {
                test.advance();
                char ch = test.get();
                if (ch == '!' || ch == '/') {
                    if (state->comment == COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_SOL)
                        state->comment = COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_MOL;
                    else if (state->comment == COMMENT_LINE_RESUME_INSIDE_STRING_SOL)
                        state->comment = COMMENT_LINE_RESUME_INSIDE_STRING_MOL;
                    else
                        state->comment = COMMENT_LINE_INSIDE_MULTI_LINE;
                    token->type = Token_Type::DOC_COMMENT;
                    token->start = iterator->position;
                    test.advance();
                    *iterator = test;
                    token->end = test.position;
                    return true;
                }
            }
            goto def;
        }

        case CZ_BLANK_CASES:
            iterator->advance();
            break;

        case CZ_SPACE_LINE_CASES:
            iterator->advance();
            // fallthrough
        default:
        def:
            comment_pop(state);
            return handle_syntax(iterator, token, state);
        }
    }
}

static bool handle_line_comment_doc(Contents_Iterator* iterator, Token* token, State* state) {
    for (;; iterator->advance()) {
        if (iterator->at_eob()) {
            token->type = Token_Type::DOC_COMMENT;
            break;
        }

    retry:
        switch (iterator->get()) {
        case '`':
            handle_line_comment_doc_tilde(iterator, token, state);
            goto ret;

        case '*': {
            iterator->advance();
            if (iterator->at_eob()) {
                token->type = Token_Type::DOC_COMMENT;
                goto ret;
            }

            if (iterator->get() == ' ') {
                if (state->comment == COMMENT_LINE_RESUME_OUTSIDE_SOL) {
                    if (iterator->position - 1 == token->start) {
                        state->comment = COMMENT_LINE_RESUME_OUTSIDE_MOL;
                        token->type = Token_Type::PUNCTUATION;
                    } else {
                        iterator->retreat();
                        token->type = Token_Type::DOC_COMMENT;
                    }
                    goto ret;
                }
            }
            goto retry;
        }

        case '#':
            if (state->comment == COMMENT_LINE_RESUME_OUTSIDE_SOL) {
                if (iterator->position == token->start) {
                    state->comment = COMMENT_LINE_RESUME_OUTSIDE_HEADER;
                    token->type = Token_Type::PUNCTUATION;
                    iterator->advance();
                } else {
                    token->type = Token_Type::DOC_COMMENT;
                }
                goto ret;
            }
            break;

        case '+':
        case '-': {
            if (state->comment == COMMENT_LINE_RESUME_OUTSIDE_SOL) {
                if (iterator->position == token->start) {
                    state->comment = COMMENT_LINE_RESUME_OUTSIDE_MOL;
                    token->type = Token_Type::PUNCTUATION;
                    iterator->advance();
                } else {
                    token->type = Token_Type::DOC_COMMENT;
                }
                goto ret;
            }
            break;
        }

        case CZ_SPACE_LINE_CASES:
            if (state->comment == COMMENT_LINE_RESUME_INSIDE) {
                if (iterator->position == token->start) {
                    return handle_line_comment_outside_multi_line(iterator, token, state);
                } else {
                    // Happens when there is stuff after a '`' block.
                    // Ex. 'start `middle` end\n'.
                    state->comment = COMMENT_LINE_OUTSIDE_MULTI_LINE;
                    token->type = Token_Type::DOC_COMMENT;
                    goto ret;
                }
            }

            comment_pop(state);
            if (iterator->position == token->start) {
                return handle_syntax(iterator, token, state);
            }
            token->type = Token_Type::DOC_COMMENT;
            goto ret;

        case CZ_BLANK_CASES:
            break;

        default:
            state->comment = COMMENT_LINE_RESUME_OUTSIDE_MOL;
            break;
        }
    }

    if (iterator->position == token->start)
        return false;

ret:
    token->end = iterator->position;
    return true;
}

static bool resume_line_comment_header(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::TITLE;
    token->start = iterator->position;

    for (; !iterator->at_eob(); iterator->advance()) {
        switch (iterator->get()) {
        case '`':
            handle_line_comment_doc_tilde(iterator, token, state);
            goto ret;

        case CZ_SPACE_LINE_CASES:
            state->comment = COMMENT_LINE_RESUME_OUTSIDE_MOL;
            goto ret;

        default:
            break;
        }
    }

    if (iterator->position == token->start)
        return false;

ret:
    token->end = iterator->position;
    return true;
}

static void handle_line_comment_doc_tilde(Contents_Iterator* iterator, Token* token, State* state) {
    if (iterator->position == token->start) {
        Contents_Iterator test = *iterator;
        test.advance();
        bool multiline = looking_at(test, "``");

        // If resuming then handle the '```' as a multi-line code block.
        if (state->comment != COMMENT_LINE_RESUME_INSIDE) {
            if (multiline) {
                comment_push(state, COMMENT_LINE_INSIDE_MULTI_LINE);
                state->syntax = SYNTAX_AT_STMT;
                iterator->advance(3);
            } else {
                comment_push(state, COMMENT_LINE_INSIDE_INLINE);
                // Most of the time `a * b` should be interpreted as an expression.
                state->syntax = SYNTAX_IN_EXPR;
                iterator->advance();
            }
            token->type = Token_Type::OPEN_PAIR;
        } else {
            state->comment = COMMENT_LINE_RESUME_OUTSIDE_MOL;
            iterator->advance(multiline ? 3 : 1);
            token->type = Token_Type::CLOSE_PAIR;
        }
    } else {
        // Already handled some text so return and wait for the next iteration.
        state->comment = COMMENT_LINE_RESUME_OUTSIDE_MOL;
        token->type = Token_Type::DOC_COMMENT;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Block comment
///////////////////////////////////////////////////////////////////////////////

static bool handle_block_comment_doc(Contents_Iterator* iterator, Token* token, State* state);
static void handle_block_comment_doc_tilde(Contents_Iterator* iterator, Token* token, State* state);
static void handle_block_comment_normal(Contents_Iterator* iterator,
                                        Token* token,
                                        State* state,
                                        bool first);

static void handle_block_comment(Contents_Iterator* iterator, Token* token, State* state) {
    if (state->comment == COMMENT_NULL && !iterator->at_eob()) {
        char ch = iterator->get();
        if (ch == '!' || ch == '*') {
            iterator->advance();
            if (ch == '*' && looking_at(*iterator, '/')) {
                // /**/
                iterator->advance();
                token->type = Token_Type::COMMENT;
                token->end = iterator->position;
                return;
            }
            state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_SOL2;
            handle_block_comment_doc(iterator, token, state);
            return;
        }
    }
    handle_block_comment_normal(iterator, token, state, true);
}

static bool at_end_of_block_comment(Contents_Iterator iterator, State state) {
    if (state.comment == COMMENT_BLOCK_INSIDE_INLINE ||
        state.comment == COMMENT_BLOCK_INSIDE_MULTI_LINE) {
        Contents_Iterator test = iterator;
        test.advance();
        if (looking_at(test, '/')) {
            return true;
        }
    }
    return false;
}

static bool resume_block_comment_normal(Contents_Iterator* iterator, Token* token, State* state) {
    token->start = iterator->position;
    handle_block_comment_normal(iterator, token, state, false);
    return token->start != token->end;
}

static void handle_block_comment_normal(Contents_Iterator* iterator,
                                        Token* token,
                                        State* state,
                                        bool first) {
    token->type = Token_Type::COMMENT;

    // Rate limit to one bucket (two at the start of the comment) to prevent
    // hanging when inserting '/*' at the start of a big buffer.  Align to
    // bucket boundaries to allow token streams to merge, which allows us to
    // avoid re-tokenizing a buffer on a miscellaneous change near the beginning.
    if (state->comment == COMMENT_NULL || state->comment == COMMENT_BLOCK_NORMAL) {
        bool found_end = false;

        for (int i = 0; i < 1 + first; ++i) {
            if (find_bucket(iterator, "*/")) {
                iterator->advance(2);
                found_end = true;
                break;
            }
        }

        if (found_end)
            comment_pop(state);
        else
            state->comment = COMMENT_BLOCK_NORMAL;
    } else {
        bool look_for_line = (state->comment == COMMENT_LINE_INSIDE_INLINE ||
                              state->comment == COMMENT_LINE_INSIDE_MULTI_LINE ||
                              state->comment == COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_MOL);
        bool multi_line = (state->comment == COMMENT_LINE_INSIDE_MULTI_LINE ||
                           state->comment == COMMENT_BLOCK_INSIDE_MULTI_LINE ||
                           state->comment == COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_MOL);
        for (int i = 0; i < 1 + first; ++i) {
            // Find the first '\n'?, '*/', or '`'.
            uint64_t min = -1;
            int minner = 0;
            Contents_Iterator temp = *iterator;
            if (look_for_line && find_bucket(&temp, '\n')) {
                if (min > temp.position) {
                    min = temp.position;
                    minner = 1;
                }
            }

            temp = *iterator;
            if (find_bucket(&temp, "*/")) {
                if (min > temp.position) {
                    min = temp.position;
                    minner = 2;
                }
            }

            temp = *iterator;
            if (multi_line ? find_bucket(&temp, "```") : find_bucket(&temp, '`')) {
                if (min > temp.position) {
                    min = temp.position;
                    minner = 3;
                }
            }

            switch (minner) {
            case 1:  // '\n'
                iterator->advance_to(min);
                if (state->comment == COMMENT_LINE_INSIDE_INLINE) {
                    comment_pop(state);
                } else {
                    // COMMENT_LINE_INSIDE_MULTI_LINE ||
                    // COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_MOL
                    state->comment = COMMENT_LINE_RESUME_INSIDE_BLOCK_COMMENT_SOL;
                }
                goto ret;

            case 2:  // '*/'
                // Include the '*/' in the comment.
                iterator->advance_to(min + 2);

                // '/* `/* */' should parse the final '*/' as ending the outer.
                if (!look_for_line)
                    comment_pop(state);
                else if (!multi_line)
                    state->comment = COMMENT_LINE_RESUME_OUTSIDE_MOL;
                goto ret;

            case 3:  // '`'/'```'
                iterator->advance_to(min);
                state->comment = COMMENT_LINE_RESUME_INSIDE;
                goto ret;

            default:
                break;
            }
        }
    }

ret:
    token->end = iterator->position;
}

static bool resume_block_comment_doc(Contents_Iterator* iterator, Token* token, State* state) {
    token->start = iterator->position;
    return handle_block_comment_doc(iterator, token, state);
}

// At end of line inside '/* ```' block.
static bool handle_block_comment_outside_multi_line(Contents_Iterator* iterator,
                                                    Token* token,
                                                    State* state) {
    if (iterator->at_eob())
        return false;
    iterator->advance();

    while (1) {
        if (iterator->at_eob())
            return false;

        switch (iterator->get()) {
        case CZ_BLANK_CASES:
            // Skip indentation.
            iterator->advance();
            break;

        case '`':
            token->start = iterator->position;
            state->comment = COMMENT_BLOCK_RESUME_INSIDE;
            handle_block_comment_doc_tilde(iterator, token, state);
            token->end = iterator->position;
            return true;

        case '*': {
            // Ignore '* ' at start of lines.
            Contents_Iterator test = *iterator;
            test.advance();
            if (!test.at_eob() && cz::is_blank(test.get())) {
                state->comment = COMMENT_BLOCK_INSIDE_MULTI_LINE;
                token->type = Token_Type::DOC_COMMENT;
                token->start = iterator->position;
                *iterator = test;
                token->end = test.position;
                return true;
            }
        }  // fallthrough

        default:
            state->comment = COMMENT_BLOCK_INSIDE_MULTI_LINE;
            return handle_syntax(iterator, token, state);
        }
    }
}

static bool handle_block_comment_doc(Contents_Iterator* iterator, Token* token, State* state) {
    bool first = true;
    for (;; iterator->advance()) {
        if (iterator->at_eob()) {
            token->type = Token_Type::DOC_COMMENT;
            break;
        }

    retry:
        // Rate limit to one bucket (two at the start of the comment) to prevent
        // hanging when inserting '/**' at the start of a big buffer.  Align to
        // bucket boundaries to allow token streams to merge, which allows us to
        // avoid re-tokenizing a buffer on a miscellaneous change near the beginning.
        if (iterator->index == 0) {
            // Note: because this is checked before advancing, if we're resuming, we'll always
            // resume at the start of a bucket so `first` will be set to `false` immediately.
            if (first) {
                first = false;
            } else {
                token->type = Token_Type::DOC_COMMENT;
                break;
            }
        }

        switch (iterator->get()) {
        case '`':
            handle_block_comment_doc_tilde(iterator, token, state);
            goto ret;

        case '*': {
            iterator->advance();
            if (iterator->at_eob()) {
                token->type = Token_Type::DOC_COMMENT;
                goto ret;
            }

            char ch = iterator->get();
            if (ch == ' ') {
                if (state->comment == COMMENT_BLOCK_RESUME_OUTSIDE_SOL1) {
                    state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_SOL2;
                } else if (state->comment == COMMENT_BLOCK_RESUME_OUTSIDE_SOL2) {
                    if (iterator->position - 1 == token->start) {
                        state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_MOL;
                        token->type = Token_Type::PUNCTUATION;
                    } else {
                        iterator->retreat();
                        token->type = Token_Type::DOC_COMMENT;
                    }
                    goto ret;
                }
            } else if (ch == '/') {
                comment_pop(state);
                iterator->advance();
                token->type = Token_Type::DOC_COMMENT;
                goto ret;
            }
            goto retry;
        }

        case '#':
            if (state->comment == COMMENT_BLOCK_RESUME_OUTSIDE_SOL1 ||
                state->comment == COMMENT_BLOCK_RESUME_OUTSIDE_SOL2) {
                if (iterator->position == token->start) {
                    state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_HEADER;
                    token->type = Token_Type::PUNCTUATION;
                    iterator->advance();
                } else {
                    token->type = Token_Type::DOC_COMMENT;
                }
                goto ret;
            }
            break;

        case '+':
        case '-': {
            if (state->comment == COMMENT_BLOCK_RESUME_OUTSIDE_SOL1 ||
                state->comment == COMMENT_BLOCK_RESUME_OUTSIDE_SOL2) {
                if (iterator->position == token->start) {
                    state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_MOL;
                    token->type = Token_Type::PUNCTUATION;
                    iterator->advance();
                } else {
                    token->type = Token_Type::DOC_COMMENT;
                }
                goto ret;
            }
            break;
        }

        case CZ_SPACE_LINE_CASES:
            state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_SOL1;
            break;

        case CZ_BLANK_CASES:
            break;

        default:
            state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_MOL;
            break;
        }
    }

    if (iterator->position == token->start)
        return false;

ret:
    token->end = iterator->position;
    return true;
}

static bool resume_block_comment_header(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::TITLE;
    token->start = iterator->position;

    for (; !iterator->at_eob(); iterator->advance()) {
        switch (iterator->get()) {
        case '`':
            handle_block_comment_doc_tilde(iterator, token, state);
            goto ret;

        case '*': {
            // Stop before '*/'
            Contents_Iterator test = *iterator;
            test.advance();
            if (test.at_eob() || test.get() == '/')
                goto ret;
            break;
        }

        case CZ_SPACE_LINE_CASES:
            state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_MOL;
            goto ret;

        default:
            break;
        }
    }

    if (iterator->position == token->start)
        return false;

ret:
    token->end = iterator->position;
    return true;
}

static void handle_block_comment_doc_tilde(Contents_Iterator* iterator,
                                           Token* token,
                                           State* state) {
    if (iterator->position == token->start) {
        Contents_Iterator test = *iterator;
        test.advance();
        bool multiline = looking_at(test, "``");

        // If resuming then handle the '```' as a multi-line code block.
        if (state->comment != COMMENT_BLOCK_RESUME_INSIDE) {
            if (multiline) {
                comment_push(state, COMMENT_BLOCK_INSIDE_MULTI_LINE);
                state->syntax = SYNTAX_AT_STMT;
                iterator->advance(3);
            } else {
                comment_push(state, COMMENT_BLOCK_INSIDE_INLINE);
                // Most of the time `a * b` should be interpreted as an expression.
                state->syntax = SYNTAX_IN_EXPR;
                iterator->advance();
            }
            token->type = Token_Type::OPEN_PAIR;
        } else {
            state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_MOL;
            iterator->advance(multiline ? 3 : 1);
            token->type = Token_Type::CLOSE_PAIR;
        }
    } else {
        // Already handled some text so return and wait for the next iteration.
        state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_MOL;
        token->type = Token_Type::DOC_COMMENT;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Character / string literal
///////////////////////////////////////////////////////////////////////////////

static void handle_char(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::STRING;
    token->start = iterator->position;

    iterator->advance();  // opening '\''
    if (iterator->at_eob())
        goto ret;

    if (iterator->get() == '\\') {  // skip escaped character
        iterator->advance();
        if (iterator->at_eob())
            goto ret;
    }

    if (cz::is_space_line(iterator->get())) {
        goto ret;
    }
    iterator->advance();

    while (!iterator->at_eob()) {
        switch (iterator->get()) {
        case '\'':
            iterator->advance();  // closing '\''
            goto ret;
        case CZ_SPACE_LINE_CASES:
            goto ret;
        default:
            iterator->advance();
            break;
        }
    }

ret:
    token->end = iterator->position;
    state->syntax = SYNTAX_IN_EXPR;
}

static void handle_string(Contents_Iterator* iterator, Token* token, State* state, bool at_start) {
    token->type = Token_Type::STRING;
    token->start = iterator->position;
    if (at_start)
        iterator->advance();

    if (state->syntax == SYNTAX_AT_RAW_STRING_LITERAL) {
        eat_raw_string_literal(iterator);
        goto ret;
    }

    for (; !iterator->at_eob(); iterator->advance()) {
        switch (iterator->get()) {
        case '"':
            iterator->advance();
            if (state->comment == COMMENT_LINE_RESUME_INSIDE_STRING_MOL)
                state->comment = COMMENT_LINE_INSIDE_MULTI_LINE;
            goto ret;

        case '\\':
            iterator->advance();
            if (iterator->at_eob())
                goto ret;
            if (iterator->get() == '\n') {
                if (state->comment == COMMENT_LINE_INSIDE_MULTI_LINE) {
                    state->comment = COMMENT_LINE_RESUME_INSIDE_STRING_SOL;
                    goto ret;
                } else if (state->comment == COMMENT_LINE_INSIDE_INLINE) {
                    goto ret;
                }
            }
            break;

        case CZ_SPACE_LINE_CASES:
            goto ret;

        default:
            break;
        }
    }

ret:
    token->end = iterator->position;
    state->syntax = SYNTAX_IN_EXPR;
}

static void eat_raw_string_literal(Contents_Iterator* iterator) {
    Contents_Iterator start = *iterator;
    for (; !iterator->at_eob(); iterator->advance()) {
        char ch = iterator->get();
        if (ch == '(')
            break;
        if (ch == ')' || ch == '\\' || cz::is_space(ch)) {
            // Invalid character, so fail by just treating this line as a string and continuing.
            end_of_line(iterator);
            return;
        }
    }

    cz::String block = {};
    block.reserve(cz::heap_allocator(), iterator->position - start.position + 2);
    block.push(')');
    iterator->contents->slice_into(start, iterator->position, &block);
    block.push('"');

    if (find(iterator, block)) {
        iterator->advance(block.len);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Preprocessor keyword (ex. define)
///////////////////////////////////////////////////////////////////////////////

namespace {
enum {
    PREPROCESSOR_KW_IF = 1,
    PREPROCESSOR_KW_ELSE = 2,
    PREPROCESSOR_KW_ENDIF = 3,
    PREPROCESSOR_KW_INCLUDE = 4,
    PREPROCESSOR_KW_DEFINE = 5,
};
}

static int look_for_preprocessor_keyword(Contents_Iterator start, uint64_t len, char first_ch);

static bool handle_preprocessor_keyword(Contents_Iterator* iterator, Token* token, State* state) {
retry:
    if (iterator->at_eob())
        return false;

    char first_ch = iterator->get();
    switch (first_ch) {
    case CZ_ALPHA_CASES:
    case '_': {
        Contents_Iterator start = *iterator;
        token->start = iterator->position;
        iterator->advance();

        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (!cz::is_alnum(ch) && ch != '_')
                break;
            iterator->advance();
        }

        token->end = iterator->position;

        uint64_t len = iterator->position - start.position;
        state->preprocessor = PREPROCESSOR_INSIDE;
        switch (look_for_preprocessor_keyword(start, len, first_ch)) {
        case PREPROCESSOR_KW_IF:
            token->type = Token_Type::PREPROCESSOR_IF;
            break;
        case PREPROCESSOR_KW_ELSE:
            token->type = Token_Type::PREPROCESSOR_ELSE;
            break;
        case PREPROCESSOR_KW_ENDIF:
            token->type = Token_Type::PREPROCESSOR_ENDIF;
            break;
        case PREPROCESSOR_KW_INCLUDE:
            token->type = Token_Type::PREPROCESSOR_KEYWORD;
            state->preprocessor = PREPROCESSOR_AFTER_INCLUDE;
            break;
        case PREPROCESSOR_KW_DEFINE:
            token->type = Token_Type::PREPROCESSOR_KEYWORD;
            state->preprocessor = PREPROCESSOR_AFTER_DEFINE;
            break;
        default:
            token->type = Token_Type::PREPROCESSOR_KEYWORD;
            break;
        }

        return true;
    }

    case CZ_SPACE_LINE_CASES:
        preprocessor_pop(state);
        return handle_syntax(iterator, token, state);

    case CZ_BLANK_CASES:
        iterator->advance();
        goto retry;

    default:
        state->preprocessor = PREPROCESSOR_INSIDE;
        return handle_syntax(iterator, token, state);
    }
}

static int look_for_preprocessor_keyword(Contents_Iterator start, uint64_t len, char first_ch) {
    switch ((len << 8) | (uint8_t)first_ch) {
    case (2 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "if"))
            return PREPROCESSOR_KW_IF;
        return 0;
    case (4 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "else"))
            return PREPROCESSOR_KW_ELSE;
        if (looking_at_no_bounds_check(start, "elif"))
            return PREPROCESSOR_KW_ELSE;
        return 0;
    case (5 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "endif"))
            return PREPROCESSOR_KW_ENDIF;
        return 0;
    case (5 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "ifdef"))
            return PREPROCESSOR_KW_IF;
        return 0;
    case (6 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "define"))
            return PREPROCESSOR_KW_DEFINE;
        return 0;
    case (6 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "ifndef"))
            return PREPROCESSOR_KW_IF;
        return 0;
    case (7 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "include"))
            return PREPROCESSOR_KW_INCLUDE;
        return 0;
    default:
        return 0;
    }
}

template <char END>
static void handle_preprocessor_include_string(Contents_Iterator* iterator, Token* token);

static bool handle_preprocessor_include(Contents_Iterator* iterator, Token* token, State* state) {
retry:
    if (iterator->at_eob())
        return false;

    char first_ch = iterator->get();
    switch (first_ch) {
    case '"':
        handle_preprocessor_include_string<'"'>(iterator, token);
        return true;
    case '<':
        handle_preprocessor_include_string<'>'>(iterator, token);
        return true;

    case CZ_SPACE_LINE_CASES:
        preprocessor_pop(state);
        return handle_syntax(iterator, token, state);

    case CZ_BLANK_CASES:
        iterator->advance();
        goto retry;

    default:
        state->preprocessor = PREPROCESSOR_INSIDE;
        return handle_syntax(iterator, token, state);
    }

    return handle_syntax(iterator, token, state);
}

template <char END>
static void handle_preprocessor_include_string(Contents_Iterator* iterator, Token* token) {
    token->type = Token_Type::STRING;
    token->start = iterator->position;
    iterator->advance();

    while (1) {
        if (iterator->at_eob())
            goto ret;

        char first_ch = iterator->get();
        switch (first_ch) {
        case END:
            iterator->advance();
            goto ret;

        case '\n': {
            Contents_Iterator prev = *iterator;
            prev.retreat();
            if (prev.get() != '\\')
                goto ret;
        }  // fallthrough

        default:
            iterator->advance();
            break;
        }
    }

ret:
    token->end = iterator->position;
}

static bool handle_preprocessor_define(Contents_Iterator* iterator, Token* token, State* state) {
retry:
    if (iterator->at_eob())
        return false;

    char first_ch = iterator->get();
    switch (first_ch) {
    case CZ_ALPHA_CASES:
    case '_': {
        token->type = Token_Type::IDENTIFIER;
        token->start = iterator->position;
        iterator->advance();

        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (!cz::is_alnum(ch) && ch != '_')
                break;
            iterator->advance();
        }

        token->end = iterator->position;

        if (!iterator->at_eob() && iterator->get() == '(')
            state->preprocessor = PREPROCESSOR_AFTER_DEFINE_PAREN;
        else
            state->preprocessor = PREPROCESSOR_INSIDE;
        return true;
    }

    case CZ_SPACE_LINE_CASES:
        preprocessor_pop(state);
        return handle_syntax(iterator, token, state);

    case CZ_BLANK_CASES:
        iterator->advance();
        goto retry;

    default:
        state->preprocessor = PREPROCESSOR_INSIDE;
        return handle_syntax(iterator, token, state);
    }
}

static bool handle_preprocessor_define_paren(Contents_Iterator* iterator,
                                             Token* token,
                                             State* state) {
retry:
    if (iterator->at_eob())
        return false;

    char first_ch = iterator->get();
    switch (first_ch) {
    case ')':
        handle_syntax(iterator, token, state);
        state->syntax = SYNTAX_AT_STMT;
        state->preprocessor = PREPROCESSOR_INSIDE;
        return true;

    case CZ_SPACE_LINE_CASES:
        preprocessor_pop(state);
        return handle_syntax(iterator, token, state);

    case CZ_BLANK_CASES:
        iterator->advance();
        goto retry;

    default:
        return handle_syntax(iterator, token, state);
    }
}

}
}
