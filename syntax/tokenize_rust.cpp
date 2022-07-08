#include "tokenize_rust.hpp"

#include <cz/char_type.hpp>
#include <cz/ptr.hpp>
#include "contents.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

bool rust_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
restart:
    if (iterator->at_eob())
        return false;

    char first = iterator->get();
    switch (first) {
    case CZ_ALPHA_CASES:
    case '_': {
        Contents_Iterator start = *iterator;
        token->start = iterator->position;
        iterator->advance();
        while (!iterator->at_eob()) {
            auto ch = iterator->get();
            if (!cz::is_alnum(ch) && ch != '_') {
                // `r#name` is an identifier
                if (!(ch == '#' && iterator->position == token->start + 1 && first == 'r'))
                    break;
            }
            iterator->advance();
        }
        token->end = iterator->position;

        token->type = Token_Type::IDENTIFIER;
        const cz::Str keywords[] = {
            "use",    "let",   "loop",   "fn",    "mut",   "pub",      "mod",  "struct", "static",
            "extern", "crate", "unsafe", "for",   "while", "ref",      "move", "const",  "in",
            "impl",   "trait", "self",   "if",    "else",  "match",    "enum", "return", "where",
            "async",  "await", "true",   "false", "break", "continue", "type", "as"};
        for (size_t i = 0; i < CZ_DIM(keywords); ++i) {
            if (matches(start, iterator->position, keywords[i]))
                token->type = Token_Type::KEYWORD;
        }
        const cz::Str types[] = {"bool", "char", "i8",  "i16",   "i32",  "i64",
                                 "i128", "u8",   "u16", "u32",   "u64",  "u128",
                                 "f32",  "f64",  "str", "usize", "isize"};
        for (size_t i = 0; i < CZ_DIM(types); ++i) {
            if (matches(start, iterator->position, types[i]))
                token->type = Token_Type::TYPE;
        }
    } break;

    case '&':
    case '.':
    case '|':
    case ':':
    case '=': {
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        if (looking_at(*iterator, first)) {
            iterator->advance();
        }
        token->end = iterator->position;
    } break;

    case '-': {
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        if (looking_at(*iterator, '=') || looking_at(*iterator, '>')) {
            iterator->advance();
        }
        token->end = iterator->position;
    } break;

    case '<':
    case '>': {
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        if (looking_at(*iterator, '=')) {
            iterator->advance();
        } else if (looking_at(*iterator, first)) {
            iterator->advance();
            if (looking_at(*iterator, '='))
                iterator->advance();
        }
        token->end = iterator->position;
    } break;

    case '+':
    case '%':
    case '*': {
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        if (looking_at(*iterator, '=')) {
            iterator->advance();
        }
        token->end = iterator->position;
    } break;

    case '/': {
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        if (looking_at(*iterator, '=')) {
            iterator->advance();
        } else if (looking_at(*iterator, '/')) {
            token->type = Token_Type::COMMENT;
            end_of_line(iterator);
        } else if (looking_at(*iterator, '*')) {
            token->type = Token_Type::COMMENT;
            iterator->advance();
            if (find(iterator, "*/"))
                iterator->advance(2);
        }
        token->end = iterator->position;
    } break;

    case ';':
    case '?':
    case ',':
    case '$':
    case '\'':
    case '#':
    case '!': {
        token->type = Token_Type::PUNCTUATION;
        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
    } break;

    case '"': {
        token->type = Token_Type::STRING;
        token->start = iterator->position;
        iterator->advance();
        while (!iterator->at_eob()) {
            char ch = iterator->get();
            iterator->advance();
            if (ch == '"')
                break;
            if (ch == '\\') {
                if (!iterator->at_eob())
                    iterator->advance();
            }
        }
        token->end = iterator->position;
    } break;

    case CZ_SPACE_CASES: {
        iterator->advance();
        goto restart;
    }

    case '{':
    case '(':
    case '[': {
        token->type = Token_Type::OPEN_PAIR;
        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
    } break;
    case '}':
    case ')':
    case ']': {
        token->type = Token_Type::CLOSE_PAIR;
        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
    } break;

    case CZ_DIGIT_CASES: {
        token->type = Token_Type::NUMBER;
        token->start = iterator->position;
        iterator->advance();
        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (ch == '.' && !looking_at(*iterator, "..")) {
                iterator->advance();
                continue;
            }
            if (!cz::is_alnum(ch))
                break;
            iterator->advance();
        }
        token->end = iterator->position;
    } break;

    default: {
        token->type = Token_Type::TYPE;
        token->start = iterator->position;
        iterator->advance();
        token->end = iterator->position;
        return false;
    }
    }
    return true;
}

}
}
