#include "tokenize_search.hpp"

#include <cz/char_type.hpp>
#include <tracy/Tracy.hpp>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

enum {
    FIRST_LINE,
    END_OF_LINE,
    END_OF_FILE_NAME,
    START_OF_FILE_LINE,
    END_OF_FILE_LINE,
    START_OF_FILE_COLUMN,
    END_OF_FILE_COLUMN,
    START_OF_RESULT,
};

bool search_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;

    if (*state == FIRST_LINE) {
        end_of_line(iterator);
        token->type = Token_Type::SEARCH_COMMAND;
        *state = END_OF_LINE;
        goto ret;
    }

    if (*state == END_OF_LINE) {
        ++token->start;
        iterator->advance();
        while (!iterator->at_eob()) {
            if (iterator->get() == ':') {
                break;
            }
            if (iterator->get() == '\n') {
                token->type = Token_Type::SEARCH_RESULT;
                *state = END_OF_LINE;
                goto ret;
            }
            iterator->advance();
        }
        token->type = Token_Type::SEARCH_FILE_NAME;
        *state = END_OF_FILE_NAME;
        goto ret;
    }

    if (*state == END_OF_FILE_NAME) {
        iterator->advance();
        token->type = Token_Type::PUNCTUATION;
        *state = START_OF_FILE_LINE;
        goto ret;
    }

    if (*state == START_OF_FILE_LINE) {
        iterator->advance();
        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (ch == ':') {
                break;
            }
            if (ch == '\n') {
                token->type = Token_Type::SEARCH_RESULT;
                *state = END_OF_LINE;
                goto ret;
            }
            if (!cz::is_digit(ch)) {
                token->type = Token_Type::SEARCH_RESULT;
                *state = START_OF_RESULT;
                goto ret;
            }
            iterator->advance();
        }
        token->type = Token_Type::SEARCH_FILE_LINE;
        *state = END_OF_FILE_LINE;
        goto ret;
    }

    if (*state == END_OF_FILE_LINE) {
        iterator->advance();
        token->type = Token_Type::PUNCTUATION;
        *state = START_OF_FILE_COLUMN;
        goto ret;
    }

    if (*state == START_OF_FILE_COLUMN) {
        iterator->advance();
        while (!iterator->at_eob()) {
            char ch = iterator->get();
            if (ch == ':') {
                break;
            }
            if (ch == '\n') {
                token->type = Token_Type::SEARCH_RESULT;
                *state = END_OF_LINE;
                goto ret;
            }
            if (!cz::is_digit(ch)) {
                token->type = Token_Type::SEARCH_RESULT;
                *state = START_OF_RESULT;
                goto ret;
            }
            iterator->advance();
        }
        token->type = Token_Type::SEARCH_FILE_COLUMN;
        *state = END_OF_FILE_COLUMN;
        goto ret;
    }

    if (*state == END_OF_FILE_COLUMN) {
        iterator->advance();
        token->type = Token_Type::PUNCTUATION;
        *state = START_OF_RESULT;
        goto ret;
    }

    if (*state == START_OF_RESULT) {
        end_of_line(iterator);
        token->type = Token_Type::SEARCH_RESULT;
        *state = END_OF_LINE;
        goto ret;
    }

ret:
    token->end = iterator->position;
    return true;
}

}
}
