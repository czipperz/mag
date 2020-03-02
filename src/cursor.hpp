#pragma once

#include <stdint.h>
#include <cz/util.hpp>

namespace mag {

struct Cursor {
    uint64_t point;
    uint64_t mark;

    uint64_t start() const { return cz::min(mark, point); }
    uint64_t end() const { return cz::max(mark, point); }
};

}
