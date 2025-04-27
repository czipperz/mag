#pragma once

#include <cz/slice.hpp>
#include "core/edit.hpp"

namespace mag {

struct Commit_Id {
    uint64_t value;

    bool operator==(const Commit_Id& other) const { return value == other.value; }
    bool operator!=(const Commit_Id& other) const { return !(*this == other); }
};

/// A set of edits.  Think of a git commit.  `Commit`s are stored on a stack.  Editing the file
/// pushes a `Commit` onto the stack, and undo pops the stack (redo repushes the original commit).
struct Commit {
    cz::Slice<const Edit> edits;
    Commit_Id id;

    void drop();
};

}
