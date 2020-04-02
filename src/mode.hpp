#pragma once

#include <stdint.h>
#include <cz/slice.hpp>

namespace mag {

struct Contents_Iterator;
struct Token;
struct Key_Map;
struct Overlay;

struct Mode {
    Key_Map* key_map;

    bool (*next_token)(Contents_Iterator* iterator /* in/out */,
                       Token* token /* out */,
                       uint64_t* state /* in/out */);

    cz::Slice<Overlay> overlays;
};

inline bool default_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    return false;
}

}
