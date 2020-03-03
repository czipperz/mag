#pragma once

#include <cz/slice.hpp>
#include "edit.hpp"

namespace mag {

struct Commit {
    cz::Slice<Edit> edits;
    uint64_t id;

    void drop();
};

}
