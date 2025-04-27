#pragma once

#include "core/face.hpp"

namespace mag {
namespace render {

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
