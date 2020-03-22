#pragma once

#include <stdint.h>
#include <cz/util.hpp>
#include "copy_chain.hpp"

namespace mag {

struct Cursor {
    uint64_t point;
    uint64_t mark;

    Copy_Chain* copy_chain;

    uint64_t start() const { return cz::min(mark, point); }
    uint64_t end() const { return cz::max(mark, point); }
};

}
