#pragma once

#include <cz/slice.hpp>
#include "edit.hpp"

namespace mag {

struct Commit_Id {
    uint64_t value;

    bool operator==(const Commit_Id& other) const { return value == other.value; }
    bool operator!=(const Commit_Id& other) const { return !(*this == other); }
};

struct Commit {
    cz::Slice<Edit> edits;
    Commit_Id id;

    void drop();
};

}
