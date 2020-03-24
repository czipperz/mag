#pragma once

namespace mag {
namespace client {

struct Cell {
    int attrs;
    char code;

    bool operator==(const Cell& other) const { return attrs == other.attrs && code == other.code; }

    bool operator!=(const Cell& other) const { return !(*this == other); }
};

}
}
