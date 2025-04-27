#include "rebase.hpp"

#include <tracy/Tracy.hpp>
#include "core/change.hpp"
#include "core/edit.hpp"

namespace mag {

static bool is_applying_insert(const Change* change, const Edit* edit) {
    // If redoing an insert, return true.  If undoing an remove, return true.
    return change->is_redo == (edit->flags & Edit::INSERT_MASK);
}

static int64_t offset_relative(const Change* previous_change,
                               const Edit* previous,
                               uint64_t current_position,
                               bool allow_merge_insert) {
    ZoneScoped;

    // Hello Sailor
    if (is_applying_insert(previous_change, previous)) {
        //      +ac +
        // Hello ac Sailor
        if (current_position >= previous->position) {
            // Note: right now allow_merge_insert === false so this branch is always taken.
            if (!allow_merge_insert ||
                current_position >= previous->position + previous->value.len()) {
                //          +b+
                // Hello ac Sbailor
                //          -a-
                // Hello ac Silor
                return previous->value.len();
            } else {
                //       +b+
                // Hello abc Sailor
                //       -c S-
                // Hello aailor
                return current_position - previous->position;
            }
        } else {
            //  +b+
            // Hebllo ac Sailor
            //  -l-
            // Helo ac Sailor
            return 0;
        }
    } else {
        //  -llo-
        // He Sailor
        if (current_position >= previous->position) {
            //    +b+
            // He Sbailor
            //    -a-
            // He Sailor
            return -(int64_t)previous->value.len();
        } else {
            //  +b+
            // Heb Sailor
            //  -l-
            // He Sailor
            return 0;
        }
    }
}

static bool offset_unmerged_edit_by_merged_edit(const Change* merged_change,
                                                const Edit* merged_edit,
                                                Edit* unmerged_edit) {
    ZoneScoped;

    if (is_applying_insert(merged_change, merged_edit)) {
        if (unmerged_edit->position <= merged_edit->position &&
            unmerged_edit->position + unmerged_edit->value.len() >= merged_edit->position) {
            return true;
        }
    } else {
        if (unmerged_edit->position <= merged_edit->position + merged_edit->value.len() &&
            unmerged_edit->position + unmerged_edit->value.len() >= merged_edit->position) {
            return true;
        }
    }

    int64_t offset = offset_relative(merged_change, merged_edit, unmerged_edit->position, false);
    unmerged_edit->position += offset;
    return false;
}

bool offset_unmerged_edit_by_merged_changes(cz::Slice<const Change> merged_changes,
                                            Edit* unmerged_edit) {
    ZoneScoped;

    for (size_t merged_change_index = 0; merged_change_index < merged_changes.len;
         ++merged_change_index) {
        const Change* merged_change = &merged_changes[merged_change_index];
        if (merged_change->is_redo) {
            for (size_t merged_edit_index = 0; merged_edit_index < merged_change->commit.edits.len;
                 ++merged_edit_index) {
                const Edit* merged_edit = &merged_change->commit.edits[merged_edit_index];
                if (offset_unmerged_edit_by_merged_edit(merged_change, merged_edit,
                                                        unmerged_edit)) {
                    return true;
                }
            }
        } else {
            for (size_t merged_edit_index = merged_change->commit.edits.len;
                 merged_edit_index-- > 0;) {
                const Edit* merged_edit = &merged_change->commit.edits[merged_edit_index];
                if (offset_unmerged_edit_by_merged_edit(merged_change, merged_edit,
                                                        unmerged_edit)) {
                    return true;
                }
            }
        }
    }

    return false;
}

}
