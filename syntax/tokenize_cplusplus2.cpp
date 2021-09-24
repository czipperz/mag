#include "tokenize_cplusplus.hpp"

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

#if 1

///////////////////////////////////////////////////////////////////////////////
// Type definitions
///////////////////////////////////////////////////////////////////////////////

namespace cpp {
enum {
    COMMENT_NULL = 0,
    COMMENT_LINE_INSIDE_INLINE = 1,
    COMMENT_LINE_INSIDE_MULTI_LINE = 2,
    COMMENT_LINE_RESUME_INSIDE = 3,
    COMMENT_LINE_RESUME_OUTSIDE_SOL = 4,
    COMMENT_LINE_RESUME_OUTSIDE_MOL = 5,
    COMMENT_LINE_RESUME_OUTSIDE_HEADER = 6,
    COMMENT_LINE_OUTSIDE_MULTI_LINE = 7,
    COMMENT_BLOCK_INSIDE_INLINE = 8,
    COMMENT_BLOCK_INSIDE_MULTI_LINE = 9,
    COMMENT_BLOCK_RESUME_INSIDE = 10,
    COMMENT_BLOCK_RESUME_OUTSIDE_SOL1 = 11,
    COMMENT_BLOCK_RESUME_OUTSIDE_SOL2 = 12,
    COMMENT_BLOCK_RESUME_OUTSIDE_MOL = 13,
    COMMENT_BLOCK_RESUME_OUTSIDE_HEADER = 14,
    COMMENT_BLOCK_OUTSIDE_MULTI_LINE = 15,
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
};

struct State {
    uint64_t comment : 4;
    uint64_t comment_saved_preprocessor : 3;
    uint64_t comment_saved_preprocessor_saved_syntax : 3;
    uint64_t comment_saved_syntax : 3;
    uint64_t preprocessor : 3;
    uint64_t preprocessor_saved_syntax : 3;
    uint64_t syntax : 3;
    uint64_t padding : 42;
};
static_assert(sizeof(State) == sizeof(uint64_t), "Must be able to cast between the two");
}
using namespace cpp;

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
static void punctuation_double(Contents_Iterator* iterator,
                               char first_ch,
                               Token* token,
                               State* state);
static void punctuation_set(Contents_Iterator* iterator, Token* token, State* state);
static void punctuation_double_set(Contents_Iterator* iterator,
                                   char first_ch,
                                   Token* token,
                                   State* state);
static void punctuation_double_or_set(Contents_Iterator* iterator,
                                      char first_ch,
                                      Token* token,
                                      State* state);

static void handle_line_comment(Contents_Iterator* iterator, Token* token, State* state);
static void handle_block_comment(Contents_Iterator* iterator, Token* token, State* state);

static void handle_char(Contents_Iterator* iterator, Token* token, State* state);
static void handle_string(Contents_Iterator* iterator, Token* token, State* state);

static bool handle_syntax(Contents_Iterator* iterator, Token* token, State* state) {
retry:
    if (iterator->at_eob())
        return false;

    char first_ch = iterator->get();
    switch (first_ch) {
    case CZ_ALPHA_CASES:
    case '_':
        handle_identifier(iterator, first_ch, token, state);
        return true;

    case CZ_DIGIT_CASES:
        handle_number(iterator, token, state);
        return true;

    case '{':
        state->syntax = SYNTAX_AT_STMT;
    case '(':
    case '[':
        token->type = Token_Type::OPEN_PAIR;
        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
        return true;
    case '}':
    case ')':
    case ']':
        token->type = Token_Type::CLOSE_PAIR;
        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
        return true;

    case '!':
    case '~':
    case ',':
        punctuation_simple(iterator, token, state);
        state->syntax = SYNTAX_IN_EXPR;
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
    case '=':
    case '^':
    case '%':
        punctuation_set(iterator, token, state);
        return true;

    case '<':
    case '>':
        punctuation_double_set(iterator, first_ch, token, state);
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
        if (!iterator->at_eob() && iterator->get() == '*')  // .*
            iterator->advance();
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
                if (!iterator->at_eob() && ch == '*')  // ->*
                    iterator->advance();
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
        handle_string(iterator, token, state);
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

static bool is_type(Contents_Iterator iterator, State* state);
static int look_for_keyword(Contents_Iterator start, uint64_t len, char first_ch);

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
        if (!cz::is_alnum(ch) && ch != '_')
            break;
        iterator->advance();
    }

    token->end = iterator->position;

    switch (look_for_keyword(start, iterator->position - start.position, first_ch)) {
    case 1:
        // Type declaration keyword so next token is the type name (ex. struct).
        token->type = Token_Type::KEYWORD;
        state->syntax = SYNTAX_AT_TYPE;
        break;
    case 2:
        // General keyword (ex. if).
        token->type = Token_Type::KEYWORD;
        state->syntax = SYNTAX_IN_EXPR;
        break;
    case 3:
        // Type keyword (ex. char).
        token->type = Token_Type::TYPE;
        state->syntax = SYNTAX_AFTER_TYPE;
        break;
    case 4:
        // General keyword that prefixes a statement (ex. static).
        token->type = Token_Type::KEYWORD;
        state->syntax = SYNTAX_AT_STMT;
        break;
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
    if (state->syntax == SYNTAX_IN_EXPR || state->syntax == SYNTAX_AFTER_TYPE)
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

static int look_for_keyword(Contents_Iterator start, uint64_t len, char first_ch) {
    switch ((len << 8) | (uint8_t)first_ch) {
    case (2 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "do"))
            return 2;
        return 0;
    case (2 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "if"))
            return 2;
        return 0;
    case (2 << 8) | (uint8_t)'o':
        if (looking_at_no_bounds_check(start, "or"))
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "and"))
            return 2;
        // return 0;
        // case (3 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "asm"))
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'f':
        if (looking_at_no_bounds_check(start, "for"))
            // return 2;
            return 4;
        return 0;
    case (3 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int"))
            return 3;
        return 0;
    case (3 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "new")) {
            // Note: treat as a type declaration keyword since it works pretty much the same way.
            // return 2;
            return 1;
        }
        // return 0;
        // case (3 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "not"))
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "try"))
            return 2;
        return 0;
    case (3 << 8) | (uint8_t)'x':
        if (looking_at_no_bounds_check(start, "xor"))
            return 2;
        return 0;
    case (4 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "auto"))
            return 3;
        return 0;
    case (4 << 8) | (uint8_t)'b':
        if (looking_at_no_bounds_check(start, "bool"))
            return 3;
        return 0;
    case (4 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "case"))
            return 2;
        // return 0;
        // case (4 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "char"))
            return 3;
        return 0;
    case (4 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "else"))
            return 2;
        // return 0;
        // case (4 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "enum"))
            return 1;
        return 0;
    case (4 << 8) | (uint8_t)'g':
        if (looking_at_no_bounds_check(start, "goto"))
            return 2;
        return 0;
    case (4 << 8) | (uint8_t)'l':
        if (looking_at_no_bounds_check(start, "long"))
            return 3;
        return 0;
    case (4 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "this"))
            return 2;
        // return 0;
        // case (4 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "true"))
            return 2;
        return 0;
    case (4 << 8) | (uint8_t)'v':
        if (looking_at_no_bounds_check(start, "void"))
            return 3;
        return 0;
    case (5 << 8) | (uint8_t)'b':
        if (looking_at_no_bounds_check(start, "bitor"))
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'b':
        if (looking_at_no_bounds_check(start, "break"))
            return 2;
        return 0;
    case (5 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "catch"))
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "class"))
            return 1;
        // return 0;
        // case (5 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "compl"))
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "const"))
            return 4;
        return 0;
    case (5 << 8) | (uint8_t)'f':
        if (looking_at_no_bounds_check(start, "false"))
            return 2;
        // return 0;
        // case (5 << 8) | (uint8_t)'f':
        if (looking_at_no_bounds_check(start, "float"))
            return 3;
        return 0;
    case (5 << 8) | (uint8_t)'o':
        if (looking_at_no_bounds_check(start, "or_eq"))
            return 2;
        return 0;
    case (5 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "short"))
            return 3;
        return 0;
    case (5 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "throw"))
            return 2;
        return 0;
    case (5 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "union"))
            return 1;
        // return 0;
        // case (5 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "using"))
            return 2;
        return 0;
    case (5 << 8) | (uint8_t)'w':
        if (looking_at_no_bounds_check(start, "while"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "and_eq"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'b':
        if (looking_at_no_bounds_check(start, "bitand"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "delete"))
            return 2;
        // return 0;
        // case (6 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "double"))
            return 3;
        return 0;
    case (6 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "export"))
            return 4;
        // return 0;
        // case (6 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "extern"))
            return 4;
        return 0;
    case (6 << 8) | (uint8_t)'f':
        if (looking_at_no_bounds_check(start, "friend"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "inline"))
            return 4;
        // return 0;
        // case (6 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int8_t"))
            return 3;
        return 0;
    case (6 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "not_eq"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'p':
        if (looking_at_no_bounds_check(start, "public"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'r':
        if (looking_at_no_bounds_check(start, "return"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "signed"))
            return 3;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "size_t"))
            return 3;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "sizeof"))
            return 2;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "static"))
            return 4;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "struct"))
            return 1;
        // return 0;
        // case (6 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "switch"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "typeid"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'x':
        if (looking_at_no_bounds_check(start, "xor_eq"))
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'_':
        if (looking_at_no_bounds_check(start, "_Pragma"))
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "alignas"))
            return 2;
        // return 0;
        // case (7 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "alignof"))
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "char8_t"))
            return 3;
        // return 0;
        // case (7 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "concept"))
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "default"))
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int16_t"))
            return 3;
        // return 0;
        // case (7 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int32_t"))
            return 3;
        // return 0;
        // case (7 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int64_t"))
            return 3;
        return 0;
    case (7 << 8) | (uint8_t)'m':
        if (looking_at_no_bounds_check(start, "mutable"))
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "nullptr"))
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'p':
        if (looking_at_no_bounds_check(start, "private"))
            return 2;
        return 0;
    case (7 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "typedef"))
            return 4;
        return 0;
    case (7 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint8_t"))
            return 3;
        return 0;
    case (7 << 8) | (uint8_t)'v':
        if (looking_at_no_bounds_check(start, "virtual"))
            return 4;
        return 0;
    case (7 << 8) | (uint8_t)'w':
        if (looking_at_no_bounds_check(start, "wchar_t"))
            return 3;
        return 0;
    case (8 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "char16_t"))
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "char32_t"))
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "co_await"))
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "co_yield"))
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "continue"))
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "decltype"))
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'e':
        if (looking_at_no_bounds_check(start, "explicit"))
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "intmax_t"))
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "intptr_t"))
            return 3;
        return 0;
    case (8 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "noexcept"))
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'o':
        if (looking_at_no_bounds_check(start, "operator"))
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'r':
        if (looking_at_no_bounds_check(start, "reflexpr"))
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'r':
        if (looking_at_no_bounds_check(start, "register"))
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'r':
        if (looking_at_no_bounds_check(start, "requires"))
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "template"))
            return 2;
        // return 0;
        // case (8 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "typename"))
            return 2;
        return 0;
    case (8 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint16_t"))
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint32_t"))
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint64_t"))
            return 3;
        // return 0;
        // case (8 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "unsigned"))
            return 3;
        return 0;
    case (8 << 8) | (uint8_t)'v':
        if (looking_at_no_bounds_check(start, "volatile"))
            return 4;
        return 0;
    case (9 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "co_return"))
            return 2;
        // return 0;
        // case (9 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "consteval"))
            return 2;
        // return 0;
        // case (9 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "constexpr"))
            return 4;
        // return 0;
        // case (9 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "constinit"))
            return 2;
        return 0;
    case (9 << 8) | (uint8_t)'n':
        if (looking_at_no_bounds_check(start, "namespace"))
            return 2;
        return 0;
    case (9 << 8) | (uint8_t)'p':
        if (looking_at_no_bounds_check(start, "protected"))
            return 2;
        // return 0;
        // case (9 << 8) | (uint8_t)'p':
        if (looking_at_no_bounds_check(start, "ptrdiff_t"))
            return 3;
        return 0;
    case (9 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uintmax_t"))
            return 3;
        // return 0;
        // case (9 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uintptr_t"))
            return 3;
        return 0;
    case (10 << 8) | (uint8_t)'c':
        if (looking_at_no_bounds_check(start, "const_cast"))
            return 2;
        return 0;
    case (11 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_fast8_t"))
            return 3;
        return 0;
    case (11 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "static_cast"))
            return 2;
        return 0;
    case (12 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "dynamic_cast"))
            return 2;
        return 0;
    case (12 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_fast16_t"))
            return 3;
        // return 0;
        // case (12 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_fast32_t"))
            return 3;
        // return 0;
        // case (12 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_fast64_t"))
            return 3;
        // return 0;
        // case (12 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_least8_t"))
            return 3;
        return 0;
    case (12 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "synchronized"))
            return 2;
        return 0;
    case (12 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "thread_local"))
            return 2;
        return 0;
    case (12 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_fast8_t"))
            return 3;
        return 0;
    case (13 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "atomic_cancel"))
            return 2;
        // return 0;
        // case (13 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "atomic_commit"))
            return 2;
        return 0;
    case (13 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_least16_t"))
            return 3;
        // return 0;
        // case (13 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_least32_t"))
            return 3;
        // return 0;
        // case (13 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "int_least64_t"))
            return 3;
        return 0;
    case (13 << 8) | (uint8_t)'s':
        if (looking_at_no_bounds_check(start, "static_assert"))
            return 2;
        return 0;
    case (13 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_fast16_t"))
            return 3;
        // return 0;
        // case (13 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_fast32_t"))
            return 3;
        // return 0;
        // case (13 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_fast64_t"))
            return 3;
        return 0;
    case (14 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_least16_t"))
            return 3;
        // return 0;
        // case (14 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_least32_t"))
            return 3;
        // return 0;
        // case (14 << 8) | (uint8_t)'u':
        if (looking_at_no_bounds_check(start, "uint_least64_t"))
            return 3;
        return 0;
    case (15 << 8) | (uint8_t)'a':
        if (looking_at_no_bounds_check(start, "atomic_noexcept"))
            return 2;
        return 0;
    case (16 << 8) | (uint8_t)'r':
        if (looking_at_no_bounds_check(start, "reinterpret_cast"))
            return 2;
        return 0;

    default:
        return 0;
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
        if (!cz::is_alnum(ch) && ch != '_')
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

static void punctuation_double(Contents_Iterator* iterator,
                               char first_ch,
                               Token* token,
                               State* state) {
    token->type = Token_Type::PUNCTUATION;
    token->start = iterator->position;
    iterator->advance();
    if (!iterator->at_eob() && iterator->get() == first_ch)
        iterator->advance();
    token->end = iterator->position;
    state->syntax = SYNTAX_IN_EXPR;
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

static void punctuation_double_set(Contents_Iterator* iterator,
                                   char first_ch,
                                   Token* token,
                                   State* state) {
    token->type = Token_Type::PUNCTUATION;
    token->start = iterator->position;
    iterator->advance();
    if (!iterator->at_eob() && iterator->get() == first_ch)
        iterator->advance();
    if (!iterator->at_eob() && iterator->get() == '=')
        iterator->advance();
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
    if (!iterator->at_eob()) {
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
    end_of_line(iterator);
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
    for (; !iterator->at_eob(); iterator->advance()) {
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

            state->comment = COMMENT_NULL;
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

static void handle_block_comment_normal(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_block_comment_doc(Contents_Iterator* iterator, Token* token, State* state);
static void handle_block_comment_doc_tilde(Contents_Iterator* iterator, Token* token, State* state);

static void handle_block_comment(Contents_Iterator* iterator, Token* token, State* state) {
    if (!iterator->at_eob()) {
        char ch = iterator->get();
        if (ch == '!' || ch == '*') {
            iterator->advance();
            state->comment = COMMENT_BLOCK_RESUME_OUTSIDE_SOL2;
            handle_block_comment_doc(iterator, token, state);
            return;
        }
    }
    handle_block_comment_normal(iterator, token, state);
}

static void handle_block_comment_normal(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::COMMENT;
    // TODO: break up the search into chunks to avoid typing /* at start of large file from hanging.
    if (search_forward(iterator, "*/")) {
        iterator->advance(2);
    }
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
            // fallthrough
        }

        default:
            return handle_syntax(iterator, token, state);
        }
    }
}

static bool handle_block_comment_doc(Contents_Iterator* iterator, Token* token, State* state) {
    for (; !iterator->at_eob(); iterator->advance()) {
    retry:
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
                state->comment = COMMENT_NULL;
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

    iterator->advance();                                 // opening '\''
    if (!iterator->at_eob() && iterator->get() == '\\')  // optional '\\'
        iterator->advance();
    forward_char(iterator);  // body character
    forward_char(iterator);  // closing '\''

    token->end = iterator->position;
    state->syntax = SYNTAX_IN_EXPR;
}

static void handle_string(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::STRING;
    token->start = iterator->position;
    iterator->advance();

    for (; !iterator->at_eob(); iterator->advance()) {
        switch (iterator->get()) {
        case '"':
            iterator->advance();
            goto ret;

        case '\\':
            iterator->advance();
            if (iterator->at_eob())
                goto ret;
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

///////////////////////////////////////////////////////////////////////////////
// Preprocessor keyword (ex. define)
///////////////////////////////////////////////////////////////////////////////

namespace cpp {
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
            return PREPROCESSOR_KW_ENDIF;
        return 0;
    case (6 << 8) | (uint8_t)'d':
        if (looking_at_no_bounds_check(start, "define"))
            return PREPROCESSOR_KW_DEFINE;
        return 0;
    case (6 << 8) | (uint8_t)'i':
        if (looking_at_no_bounds_check(start, "ifndef"))
            return PREPROCESSOR_KW_ENDIF;
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

        case '\n':
            Contents_Iterator prev = *iterator;
            prev.retreat();
            if (prev.get() != '\\')
                goto ret;
            // fallthrough

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

#endif

}
}
