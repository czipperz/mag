#include "movement.hpp"

#include <ctype.h>
#include "buffer.hpp"

namespace mag {

uint64_t start_of_line(Buffer* buffer, uint64_t point) {
    if (point == 0) {
        return point;
    }

    --point;
    while (buffer->contents.get_once(point) != '\n') {
        if (point == 0) {
            return point;
        }
        --point;
    }
    ++point;
    return point;
}

uint64_t end_of_line(Buffer* buffer, uint64_t point) {
    while (point < buffer->contents.len && buffer->contents.get_once(point) != '\n') {
        ++point;
    }
    return point;
}

uint64_t start_of_line_text(Buffer* buffer, uint64_t point) {
    point = start_of_line(buffer, point);
    while (point < buffer->contents.len && isblank(buffer->contents.get_once(point))) {
        ++point;
    }
    return point;
}

uint64_t forward_line(Buffer* buffer, uint64_t point) {
    uint64_t start = start_of_line(buffer, point);
    uint64_t end = end_of_line(buffer, point);
    uint64_t column = point - start;
    if (end == buffer->contents.len) {
        return point;
    }
    uint64_t next_end = end_of_line(buffer, end + 1);
    return cz::min(next_end, end + 1 + column);
}

uint64_t backward_line(Buffer* buffer, uint64_t point) {
    uint64_t start = start_of_line(buffer, point);
    uint64_t column = point - start;
    if (start == 0) {
        return point;
    }
    uint64_t previous_start = start_of_line(buffer, start - 1);
    return cz::min(start - 1, previous_start + column);
}

uint64_t forward_word(Buffer* buffer, uint64_t point) {
    if (point == buffer->contents.len) {
        return point;
    }
    ++point;
    while (point < buffer->contents.len && !isalnum(buffer->contents.get_once(point))) {
        ++point;
    }
    while (point < buffer->contents.len && isalnum(buffer->contents.get_once(point))) {
        ++point;
    }
    return point;
}

uint64_t backward_word(Buffer* buffer, uint64_t point) {
    if (point == 0) {
        return point;
    }
    --point;
    while (!isalnum(buffer->contents.get_once(point))) {
        if (point == 0) {
            return point;
        }
        --point;
    }
    while (isalnum(buffer->contents.get_once(point))) {
        if (point == 0) {
            return point;
        }
        --point;
    }
    ++point;
    return point;
}

uint64_t forward_char(Buffer* buffer, uint64_t point) {
    if (point < buffer->contents.len) {
        return point + 1;
    } else {
        return point;
    }
}

uint64_t backward_char(Buffer* buffer, uint64_t point) {
    if (point > 0) {
        return point - 1;
    } else {
        return point;
    }
}

}
