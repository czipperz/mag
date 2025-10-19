#include "eat.hpp"

#include "core/contents.hpp"
#include "core/match.hpp"

namespace mag {
struct Contents_Iterator;

bool eat_character(Contents_Iterator* iterator, char ch) {
    if (!looking_at(*iterator, ch))
        return false;
    iterator->advance();
    return true;
}

bool eat_string(Contents_Iterator* iterator, cz::Str str) {
    if (!looking_at(*iterator, str))
        return false;
    iterator->advance(str.len);
    return true;
}

bool eat_number(Contents_Iterator* iterator) {
    if (iterator->at_eob() || !cz::is_digit(iterator->get()))
        return false;
    do {
        iterator->advance();
    } while (!iterator->at_eob() && cz::is_digit(iterator->get()));
    return true;
}

}
