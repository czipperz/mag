#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mag {
namespace client {

struct Cell {
    struct Attrs {
        size_t color;
        uint32_t flags;
    } attrs;
    char code;

    bool operator==(const Cell& other) const {
        return attrs.color == other.attrs.color && attrs.flags == other.attrs.flags &&
               code == other.code;
    }
    bool operator!=(const Cell& other) const { return !(*this == other); }
};

}
}
