#include "common.hpp"

#include <cz/char_type.hpp>
#include "contents.hpp"

namespace mag {
namespace syntax {

bool advance_whitespace(Contents_Iterator* iterator) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (!cz::is_space(ch)) {
            return true;
        }
        iterator->advance();
    }
}

}
}
