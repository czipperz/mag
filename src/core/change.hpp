#pragma once

#include "core/commit.hpp"

namespace mag {

/// `Change` displays a sequential, chronological view of the changes to a buffer's
/// contents.  Undo and redo both add changes.  This is used for the purpose of
/// "update listeners" that want to know when the buffer is changing.  For example,
/// the graphical stack uses changes to know when to redraw the buffer.
struct Change {
    Commit commit;
    bool is_redo;
};

void position_after_change(const Change& change, uint64_t* position);
void position_before_change(const Change& change, uint64_t* position);

void position_after_changes(cz::Slice<const Change> changes, uint64_t* position);
void position_before_changes(cz::Slice<const Change> changes, uint64_t* position);

}
