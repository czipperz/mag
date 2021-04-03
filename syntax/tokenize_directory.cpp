#include "tokenize_directory.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

enum {
    FIRST_LINE,
    AT_FILE_TIME,
    AT_FILE_DIRECTORY,
    AT_FILE_NAME,
};

bool directory_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (iterator->contents->len <= 27 || iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;

    if (*state == FIRST_LINE) {
        // :DirectorySortFormat
        if (iterator->position == 0) {
            iterator->advance(17);
            if (looking_at(*iterator, " (V)")) {
                iterator->advance(4);
                token->type = Token_Type::DIRECTORY_SELECTED_COLUMN;
            } else {
                token->type = Token_Type::DIRECTORY_COLUMN;
            }
            goto ret;
        } else {
            iterator->advance_to(22);
            token->start = iterator->position;
            iterator->advance(4);
            if (looking_at(*iterator, " (V)")) {
                iterator->advance(4);
                token->type = Token_Type::DIRECTORY_SELECTED_COLUMN;
            } else {
                token->type = Token_Type::DIRECTORY_COLUMN;
            }

            *state = AT_FILE_TIME;

            goto ret;
        }
    }

    if (*state == AT_FILE_TIME) {
        iterator->advance();  // newline
        if (iterator->position + 19 >= iterator->contents->len) {
            return false;
        }
        token->start = iterator->position;
        iterator->advance(19);
        token->type = Token_Type::DIRECTORY_FILE_TIME;
        *state = AT_FILE_DIRECTORY;
        goto ret;
    }

    if (*state == AT_FILE_DIRECTORY) {
        iterator->advance();
        if (iterator->get() == '/') {
            token->start = iterator->position;
            iterator->advance();
            token->type = Token_Type::DIRECTORY_FILE_DIRECTORY;
            *state = AT_FILE_NAME;
            goto ret;
        }
    }

    if (!advance_whitespace(iterator)) {
        return false;
    }

    token->start = iterator->position;
    end_of_line(iterator);
    token->type = Token_Type::DIRECTORY_FILE_NAME;
    *state = AT_FILE_TIME;

ret:
    token->end = iterator->position;
    return true;
}

}
}
