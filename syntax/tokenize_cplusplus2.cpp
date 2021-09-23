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

///////////////////////////////////////////////////////////////////////////////
// Type definitions
///////////////////////////////////////////////////////////////////////////////

namespace cpp {
enum {
    TOP_SYNTAX = 0,
    TOP_LINE_COMMENT_INSIDE_INLINE = 1,
    TOP_LINE_COMMENT_INSIDE_MULTI_LINE = 2,
    TOP_LINE_COMMENT_RESUME_INSIDE = 3,
    TOP_LINE_COMMENT_RESUME_OUTSIDE = 4,
    TOP_LINE_COMMENT_OUTSIDE_MULTI_LINE = 5,
};

enum {
    MIDDLE_SYNTAX = 0,
    MIDDLE_PREPROCESSOR_AT_KEYWORD = 1,
    MIDDLE_PREPROCESSOR_INSIDE = 2,
    MIDDLE_PREPROCESSOR_AFTER_INCLUDE = 3,
    MIDDLE_PREPROCESSOR_AFTER_DEFINE = 4,
    MIDDLE_PREPROCESSOR_AFTER_DEFINE_PAREN = 5,
};

enum {
    SYNTAX_AT_STMT = 0,
    SYNTAX_IN_EXPR = 1,
    SYNTAX_AT_TYPE = 2,
    SYNTAX_AFTER_TYPE = 3,
    SYNTAX_AFTER_DECL = 4,
};

struct State {
    uint64_t top : 3;
    uint64_t middle : 3;
    uint64_t syntax : 3;
    uint64_t saved_syntax : 3;
    uint64_t comment : 3;
    uint64_t padding : 49;
};
static_assert(sizeof(State) == sizeof(uint64_t), "Must be able to cast between the two");
}
using namespace cpp;

///////////////////////////////////////////////////////////////////////////////
// cpp_next_token
///////////////////////////////////////////////////////////////////////////////

static bool handle_top(Contents_Iterator* iterator, Token* token, State* state);

bool cpp_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state_) {
    ZoneScoped;
    State* state = (State*)state_;
    return handle_top(iterator, token, state);
}

///////////////////////////////////////////////////////////////////////////////
// handle_top
///////////////////////////////////////////////////////////////////////////////

static bool handle_middle(Contents_Iterator* iterator, Token* token, State* state);
static bool resume_line_comment_doc(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_line_comment_outside_multi_line(Contents_Iterator* iterator,
                                                   Token* token,
                                                   State* state);

static bool handle_top(Contents_Iterator* iterator, Token* token, State* state) {
    switch (state->top) {
    case TOP_SYNTAX:
    case TOP_LINE_COMMENT_INSIDE_INLINE:
    case TOP_LINE_COMMENT_INSIDE_MULTI_LINE:
        return handle_middle(iterator, token, state);
    case TOP_LINE_COMMENT_RESUME_INSIDE:
    case TOP_LINE_COMMENT_RESUME_OUTSIDE:
        return resume_line_comment_doc(iterator, token, state);
    case TOP_LINE_COMMENT_OUTSIDE_MULTI_LINE:
        return handle_line_comment_outside_multi_line(iterator, token, state);
    default:
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////
// handle_middle
///////////////////////////////////////////////////////////////////////////////

static bool handle_syntax(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_preprocessor_keyword(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_preprocessor_include(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_preprocessor_define(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_preprocessor_define_paren(Contents_Iterator* iterator,
                                             Token* token,
                                             State* state);

static bool handle_middle(Contents_Iterator* iterator, Token* token, State* state) {
    switch (state->middle) {
    case MIDDLE_SYNTAX:
    case MIDDLE_PREPROCESSOR_INSIDE:
        return handle_syntax(iterator, token, state);
    case MIDDLE_PREPROCESSOR_AT_KEYWORD:
        return handle_preprocessor_keyword(iterator, token, state);
    case MIDDLE_PREPROCESSOR_AFTER_INCLUDE:
        return handle_preprocessor_include(iterator, token, state);
    case MIDDLE_PREPROCESSOR_AFTER_DEFINE:
        return handle_preprocessor_define(iterator, token, state);
    case MIDDLE_PREPROCESSOR_AFTER_DEFINE_PAREN:
        return handle_preprocessor_define_paren(iterator, token, state);
    default:
        return false;
    }
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
        if (state->middle != MIDDLE_PREPROCESSOR_INSIDE) {
            state->middle = MIDDLE_PREPROCESSOR_AT_KEYWORD;
            state->saved_syntax = state->syntax;
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
        if (state->top == TOP_LINE_COMMENT_INSIDE_INLINE ||
            state->top == TOP_LINE_COMMENT_INSIDE_MULTI_LINE) {
            state->top = TOP_LINE_COMMENT_RESUME_INSIDE;
            return resume_line_comment_doc(iterator, token, state);
        }
        iterator->advance();
        goto retry;

    case CZ_SPACE_LINE_CASES:
        if (state->middle == MIDDLE_PREPROCESSOR_INSIDE) {
            state->middle = MIDDLE_SYNTAX;
            state->syntax = state->saved_syntax;
        }
        if (state->top == TOP_LINE_COMMENT_INSIDE_INLINE ||
            state->top == TOP_LINE_COMMENT_INSIDE_MULTI_LINE) {
            state->top = TOP_LINE_COMMENT_RESUME_INSIDE;
            return resume_line_comment_doc(iterator, token, state);
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
            if (state->middle == MIDDLE_SYNTAX)
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
            // return 2;
            return 4;
        return 0;
    case (6 << 8) | (uint8_t)'t':
        if (looking_at_no_bounds_check(start, "typeid"))
            return 2;
        return 0;
    case (6 << 8) | (uint8_t)'x':
        if (looking_at_no_bounds_check(start, "xor_eq"))
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
            return 2;
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
// Comment
///////////////////////////////////////////////////////////////////////////////

static void handle_line_comment_normal(Contents_Iterator* iterator, Token* token, State* state);
static bool handle_line_comment_doc(Contents_Iterator* iterator, Token* token, State* state);
static void handle_block_comment_normal(Contents_Iterator* iterator, Token* token, State* state);
static void handle_block_comment_doc(Contents_Iterator* iterator, Token* token, State* state);

static void handle_line_comment(Contents_Iterator* iterator, Token* token, State* state) {
    if (!iterator->at_eob()) {
        char ch = iterator->get();
        if (ch == '!' || ch == '/') {
            iterator->advance();
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

static bool handle_line_comment_outside_multi_line(Contents_Iterator* iterator,
                                                   Token* token,
                                                   State* state) {
    // At end of line inside '/// ```' block.
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
                    state->top = TOP_LINE_COMMENT_INSIDE_MULTI_LINE;
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

        case CZ_SPACE_LINE_CASES:
            iterator->advance();
            // fallthrough
        default:
        def:
            state->top = TOP_SYNTAX;
            state->syntax = state->saved_syntax;
            return handle_syntax(iterator, token, state);
        }
    }
}

static bool handle_line_comment_doc(Contents_Iterator* iterator, Token* token, State* state) {
    for (; !iterator->at_eob(); iterator->advance()) {
        switch (iterator->get()) {
        case '`':
            if (iterator->position == token->start) {
                Contents_Iterator test = *iterator;
                test.advance();
                if (looking_at(test, "``")) {
                    // If resuming then handle the '```' as a multi-line code block.
                    if (state->top == TOP_LINE_COMMENT_RESUME_OUTSIDE) {
                        state->top = TOP_LINE_COMMENT_INSIDE_MULTI_LINE;
                        state->saved_syntax = state->syntax;
                        // Most of the time `a * b` should be interpreted as an expression.
                        state->syntax = SYNTAX_AT_STMT;
                        token->type = Token_Type::OPEN_PAIR;
                        iterator->advance(3);
                        token->end = iterator->position;
                        return true;
                    } else {
                        state->top = TOP_LINE_COMMENT_RESUME_OUTSIDE;
                        token->type = Token_Type::CLOSE_PAIR;
                        iterator->advance(3);
                        token->end = iterator->position;
                        return true;
                    }
                } else {
                    // If resuming then handle the '`' as an inline code block.
                    if (state->top == TOP_LINE_COMMENT_RESUME_OUTSIDE) {
                        state->top = TOP_LINE_COMMENT_INSIDE_INLINE;
                        state->saved_syntax = state->syntax;
                        // Most of the time `a * b` should be interpreted as an expression.
                        state->syntax = SYNTAX_IN_EXPR;
                        token->type = Token_Type::OPEN_PAIR;
                        iterator->advance();
                        token->end = iterator->position;
                        return true;
                    } else {
                        state->top = TOP_LINE_COMMENT_RESUME_OUTSIDE;
                        token->type = Token_Type::CLOSE_PAIR;
                        iterator->advance();
                        token->end = iterator->position;
                        return true;
                    }
                }
            } else {
                // Already handled some text so return and wait for the next iteration.
                state->top = TOP_LINE_COMMENT_RESUME_OUTSIDE;
                token->type = Token_Type::DOC_COMMENT;
                token->end = iterator->position;
                return true;
            }

        case CZ_SPACE_LINE_CASES:
            if (state->top == TOP_LINE_COMMENT_RESUME_INSIDE) {
                if (iterator->position == token->start) {
                    return handle_line_comment_outside_multi_line(iterator, token, state);
                } else {
                    // Happens when there is stuff after a '`' block.
                    // Ex. 'start `middle` end\n'.
                    state->top = TOP_LINE_COMMENT_OUTSIDE_MULTI_LINE;
                    token->type = Token_Type::DOC_COMMENT;
                    token->end = iterator->position;
                    return true;
                }
            }

            state->top = TOP_SYNTAX;
            if (iterator->position == token->start) {
                return handle_syntax(iterator, token, state);
            }
            token->type = Token_Type::DOC_COMMENT;
            token->end = iterator->position;
            return true;

        default:
            break;
        }
    }

    if (iterator->position == token->start) {
        token->end = iterator->position;
        return true;
    }
    return false;
}

static void handle_block_comment(Contents_Iterator* iterator, Token* token, State* state) {
    if (!iterator->at_eob()) {
        char ch = iterator->get();
        if (ch == '!' || ch == '*') {
            iterator->advance();
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

static void handle_block_comment_doc(Contents_Iterator* iterator, Token* token, State* state) {
    token->type = Token_Type::DOC_COMMENT;
    CZ_PANIC("TODO");  // TODO
    if (search_forward(iterator, "*/")) {
        iterator->advance(2);
    }
    token->end = iterator->position;
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
        state->middle = MIDDLE_PREPROCESSOR_INSIDE;
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
            state->middle = MIDDLE_PREPROCESSOR_AFTER_INCLUDE;
            break;
        case PREPROCESSOR_KW_DEFINE:
            token->type = Token_Type::PREPROCESSOR_KEYWORD;
            state->middle = MIDDLE_PREPROCESSOR_AFTER_DEFINE;
            break;
        default:
            token->type = Token_Type::PREPROCESSOR_KEYWORD;
            break;
        }

        return true;
    }

    case CZ_SPACE_LINE_CASES:
        state->middle = MIDDLE_SYNTAX;
        state->syntax = state->saved_syntax;
        return handle_syntax(iterator, token, state);

    case CZ_BLANK_CASES:
        iterator->advance();
        goto retry;

    default:
        state->middle = MIDDLE_PREPROCESSOR_INSIDE;
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
        state->middle = MIDDLE_SYNTAX;
        state->syntax = state->saved_syntax;
        return handle_syntax(iterator, token, state);

    case CZ_BLANK_CASES:
        iterator->advance();
        goto retry;

    default:
        state->middle = MIDDLE_PREPROCESSOR_INSIDE;
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
            state->middle = MIDDLE_PREPROCESSOR_AFTER_DEFINE_PAREN;
        else
            state->middle = MIDDLE_PREPROCESSOR_INSIDE;
        return true;
    }

    case CZ_SPACE_LINE_CASES:
        state->middle = MIDDLE_SYNTAX;
        state->syntax = state->saved_syntax;
        return handle_syntax(iterator, token, state);

    case CZ_BLANK_CASES:
        iterator->advance();
        goto retry;

    default:
        state->middle = MIDDLE_PREPROCESSOR_INSIDE;
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
        state->middle = MIDDLE_PREPROCESSOR_INSIDE;
        return true;

    case CZ_SPACE_LINE_CASES:
        state->middle = MIDDLE_SYNTAX;
        state->syntax = state->saved_syntax;
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
