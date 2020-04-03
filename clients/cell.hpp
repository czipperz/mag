#pragma once

#include <stddef.h>
#include <stdint.h>
#include "theme.hpp"

namespace mag {
namespace client {

struct Cell {
    Face face;
    char code;

    bool operator==(const Cell& other) const {
        return face.foreground == other.face.foreground &&
               face.background == other.face.background && face.flags == other.face.flags &&
               code == other.code;
    }
    bool operator!=(const Cell& other) const { return !(*this == other); }
};

}
}
