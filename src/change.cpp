#include "change.hpp"

namespace mag {

void position_after_change(Change* change, uint64_t* position) {
    if (change->is_redo) {
        position_after_edits(change->commit.edits, position);
    } else {
        position_before_edits(change->commit.edits, position);
    }
}

void position_before_change(Change* change, uint64_t* position) {
    if (change->is_redo) {
        position_before_edits(change->commit.edits, position);
    } else {
        position_after_edits(change->commit.edits, position);
    }
}

void position_after_changes(cz::Slice<Change> changes, uint64_t* position) {
    for (size_t i = 0; i < changes.len; ++i) {
        position_after_change(&changes[i], position);
    }
}

void position_before_changes(cz::Slice<Change> changes, uint64_t* position) {
    for (size_t i = changes.len; i-- > 0;) {
        position_before_change(&changes[i], position);
    }
}

}
