#pragma once

#include <cz/str.hpp>

namespace mag {
struct Contents_Iterator;

bool eat_character(Contents_Iterator* iterator, char ch);
bool eat_string(Contents_Iterator* iterator, cz::Str str);
bool eat_number(Contents_Iterator* iterator);

}
