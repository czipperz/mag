#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ssostr.hpp"

namespace mag {

struct Edit {
    enum Flags {
        /// Is the edit inserting or removing text?
        ///
        /// Note that when undo is used, this flag will be inverted such that INSERT edits will be
        /// applied as removals and vice versa.
        INSERT_MASK = 1,
        /// If the edit is applied as an insertion, is it applied before or after the point?
        ///
        /// If after the point, the point will not be moved when the position of the insertion is
        /// the same as the point.  Otherwise it will be incremented by the length of the insertion.
        AFTER_POSITION_MASK = 2,

        REMOVE = 0,
        INSERT = 1,
        REMOVE_AFTER_POSITION = 2,
        INSERT_AFTER_POSITION = 3,
    };

    SSOStr value;
    uint64_t position;
    Flags flags;
};

void position_after_edit(const Edit& edit, uint64_t* position);
void position_before_edit(const Edit& edit, uint64_t* position);

void position_after_edits(cz::Slice<const Edit> edits, uint64_t* position);
void position_before_edits(cz::Slice<const Edit> edits, uint64_t* position);

}
