#pragma once

#include <cz/slice.hpp>

namespace mag {
struct Change;
struct Edit;

bool offset_unmerged_edit_by_merged_changes(cz::Slice<const Change> merged_changes,
                                            Edit* unmerged_edit);

}
