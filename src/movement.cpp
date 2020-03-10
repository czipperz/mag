#include "movement.hpp"

#include <ctype.h>
#include "buffer.hpp"

namespace mag {

void start_of_line(Buffer* buffer, Contents_Iterator* iterator) {
    if (iterator->at_bob()) {
        return;
    }

    iterator->retreat();
    while (iterator->get() != '\n') {
        if (iterator->at_bob()) {
            return;
        }
        iterator->retreat();
    }
    iterator->advance();
}

void end_of_line(Buffer* buffer, Contents_Iterator* iterator) {
    while (!iterator->at_eob() && iterator->get() != '\n') {
        iterator->advance();
    }
}

void start_of_line_text(Buffer* buffer, Contents_Iterator* iterator) {
    start_of_line(buffer, iterator);
    while (!iterator->at_eob() && isblank(iterator->get())) {
        iterator->advance();
    }
}

void forward_line(Buffer* buffer, Contents_Iterator* iterator) {
    Contents_Iterator start = *iterator;
    Contents_Iterator end = *iterator;
    start_of_line(buffer, &start);
    end_of_line(buffer, &end);
    uint64_t column = iterator->position - start.position;
    if (end.at_eob()) {
        return;
    }

    Contents_Iterator next_end = end;
    next_end.advance();
    end_of_line(buffer, &next_end);
    if (next_end.position < end.position + 1 + column) {
        *iterator = next_end;
    } else {
        end.advance(column + 1);
        *iterator = end;
    }
}

void backward_line(Buffer* buffer, Contents_Iterator* iterator) {
    Contents_Iterator start = *iterator;
    start_of_line(buffer, &start);
    uint64_t column = iterator->position - start.position;
    if (start.at_bob()) {
        return;
    }

    Contents_Iterator previous_start = start;
    previous_start.retreat();
    start_of_line(buffer, &previous_start);
    if (start.position - 1 < previous_start.position + column) {
        start.retreat();
        *iterator = start;
    } else {
        previous_start.advance(column);
        *iterator = previous_start;
    }
}

void forward_word(Buffer* buffer, Contents_Iterator* iterator) {
    if (iterator->at_eob()) {
        return;
    }
    iterator->advance();
    while (!iterator->at_eob() && !isalnum(iterator->get())) {
        iterator->advance();
    }
    while (!iterator->at_eob() && isalnum(iterator->get())) {
        iterator->advance();
    }
    return;
}

void backward_word(Buffer* buffer, Contents_Iterator* iterator) {
    if (iterator->at_bob()) {
        return;
    }
    iterator->retreat();
    while (!isalnum(iterator->get())) {
        if (iterator->at_bob()) {
            return;
        }
        iterator->retreat();
    }
    while (isalnum(iterator->get())) {
        if (iterator->at_bob()) {
            return;
        }
        iterator->retreat();
    }
    iterator->advance();
}

void forward_char(Buffer* buffer, Contents_Iterator* iterator) {
    if (!iterator->at_eob()) {
        iterator->advance();
    }
}

void backward_char(Buffer* buffer, Contents_Iterator* iterator) {
    if (!iterator->at_bob()) {
        iterator->retreat();
    }
}

}
