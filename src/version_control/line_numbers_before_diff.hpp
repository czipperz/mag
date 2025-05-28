#pragma once

#include <cz/slice.hpp>
#include <cz/str.hpp>

namespace mag {
namespace version_control {

/// Take the line numbers as they currently occur in the path (ie unstaged changes) and
/// transform them to what they would be in HEAD.  This is needed to get correct line numbers
/// to feed to `git log`.  This runs `git diff -U0 HEAD:$path $path` and then feeds it into
/// `line_numbers_before_diff`.  If a line was added in the diff then the new line number will
/// be before the added section.  Returns false on failure to run git diff or parsing error.
bool line_numbers_before_changes_to_path(const char* working_directory,
                                         cz::Str path,
                                         cz::Slice<uint64_t> line_numbers);

/// Same as above but allows for a custom diff.  Returns false on parsing error.
bool line_numbers_before_diff(cz::Str diff_output, cz::Slice<uint64_t> line_numbers);

/// Parse git patch line to extract the following fields:
/// ```
/// @@ -before_line[,before_len] +after_line[,after_len] @@
/// ```
bool parse_diff_line_numbers(cz::Str line,
                             uint64_t* before_line,
                             uint64_t* before_len,
                             uint64_t* after_line,
                             uint64_t* after_len);

}
}
