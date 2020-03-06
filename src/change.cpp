#include "change.hpp"

namespace mag {

void Change::adjust_position(uint64_t* position) {
    if (is_redo) {
        position_after_edits(commit.edits, position);
    } else {
        position_before_edits(commit.edits, position);
    }
}

}
