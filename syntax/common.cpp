#include "common.hpp"

#include <ctype.h>
#include "contents.hpp"

namespace mag {
namespace syntax {

bool advance_whitespace(Contents_Iterator* iterator) {
    while (1) {
        if (iterator->at_eob()) {
            return false;
        }

        char ch = iterator->get();
        if (!isspace(ch)) {
            return true;
        }
        iterator->advance();
    }
}

}
}
