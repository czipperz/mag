#include "tokenizer_check_point.hpp"

#include <cz/heap.hpp>
#include "token.hpp"

namespace mag {
namespace client {

bool next_check_point(Window_Cache* window_cache,
                      Buffer* buffer,
                      Contents_Iterator* iterator,
                      uint64_t* state,
                      cz::Vector<Tokenizer_Check_Point>* check_points) {
    ZoneScoped;

    uint64_t start_position = iterator->position;
    while (!iterator->at_eob()) {
        if (iterator->position >= start_position + 1024) {
            Tokenizer_Check_Point check_point;
            check_point.position = iterator->position;
            check_point.state = *state;
            check_points->reserve(cz::heap_allocator(), 1);
            check_points->push(check_point);
            return true;
        }

        Token token;
        if (!buffer->mode.next_token(&buffer->contents, iterator, &token, state)) {
            break;
        }
    }

    return false;
}

}
}
