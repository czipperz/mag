#pragma once

#include "commit.hpp"

namespace mag {

struct Change {
    Commit commit;
    bool is_redo;
};

void position_after_change(const Change& change, uint64_t* position);
void position_before_change(const Change& change, uint64_t* position);

void position_after_changes(cz::Slice<const Change> changes, uint64_t* position);
void position_before_changes(cz::Slice<const Change> changes, uint64_t* position);

}
