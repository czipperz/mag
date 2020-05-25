#include "movement.hpp"

#include <ctype.h>
#include "buffer.hpp"

namespace mag {

void start_of_line(Contents_Iterator* iterator) {
    do {
        if (iterator->at_bob()) {
            return;
        }

        iterator->retreat();
    } while (iterator->get() != '\n');

    iterator->advance();
}

void end_of_line(Contents_Iterator* iterator) {
    while (!iterator->at_eob() && iterator->get() != '\n') {
        iterator->advance();
    }
}

void forward_through_whitespace(Contents_Iterator* iterator) {
    while (!iterator->at_eob() && isblank(iterator->get())) {
        iterator->advance();
    }
}

void backward_through_whitespace(Contents_Iterator* iterator) {
    do {
        if (iterator->at_bob()) {
            return;
        }

        iterator->retreat();
    } while (isblank(iterator->get()));

    iterator->advance();
}

void start_of_line_text(Contents_Iterator* iterator) {
    start_of_line(iterator);
    forward_through_whitespace(iterator);
}

void forward_line(Contents_Iterator* iterator) {
    Contents_Iterator start = *iterator;
    Contents_Iterator end = *iterator;
    start_of_line(&start);
    end_of_line(&end);
    uint64_t column = iterator->position - start.position;
    if (end.at_eob()) {
        return;
    }

    Contents_Iterator next_end = end;
    next_end.advance();
    end_of_line(&next_end);
    if (next_end.position < end.position + 1 + column) {
        *iterator = next_end;
    } else {
        end.advance(column + 1);
        *iterator = end;
    }
}

void backward_line(Contents_Iterator* iterator) {
    Contents_Iterator start = *iterator;
    start_of_line(&start);
    uint64_t column = iterator->position - start.position;
    if (start.at_bob()) {
        return;
    }

    Contents_Iterator previous_start = start;
    previous_start.retreat();
    start_of_line(&previous_start);
    if (start.position - 1 < previous_start.position + column) {
        start.retreat();
        *iterator = start;
    } else {
        previous_start.advance(column);
        *iterator = previous_start;
    }
}

void forward_word(Contents_Iterator* iterator) {
    if (iterator->at_eob()) {
        return;
    }
    while (!iterator->at_eob() && !isalnum(iterator->get())) {
        iterator->advance();
    }
    while (!iterator->at_eob() && isalnum(iterator->get())) {
        iterator->advance();
    }
    return;
}

void backward_word(Contents_Iterator* iterator) {
    do {
        if (iterator->at_bob()) {
            return;
        }
        iterator->retreat();
    } while (!isalnum(iterator->get()));

    iterator->retreat();
    while (isalnum(iterator->get())) {
        if (iterator->at_bob()) {
            return;
        }
        iterator->retreat();
    }

    iterator->advance();
}

void forward_char(Contents_Iterator* iterator) {
    if (!iterator->at_eob()) {
        iterator->advance();
    }
}

void backward_char(Contents_Iterator* iterator) {
    if (!iterator->at_bob()) {
        iterator->retreat();
    }
}

}
