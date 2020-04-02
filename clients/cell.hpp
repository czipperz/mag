#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mag {
namespace client {

struct Cell {
    struct Attrs {
        int16_t foreground = -1;
        int16_t background = -1;
        uint32_t flags;
    } attrs;
    char code;

    bool operator==(const Cell& other) const {
        return attrs.foreground == other.attrs.foreground &&
               attrs.background == other.attrs.background && attrs.flags == other.attrs.flags &&
               code == other.code;
    }
    bool operator!=(const Cell& other) const { return !(*this == other); }
};

}
}
