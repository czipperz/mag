#pragma once

#include "contents.hpp"
#include "mode.hpp"
#include "transaction.hpp"

namespace mag {

/// Get the visual column that line comments should be inserted at
/// when trying to insert line comments for every line in the region.
///
/// Accounts for lines that have mixed tabs and spaces.  Ignores empty
/// lines, assuming that they will be padded to the resulting column.
uint64_t visual_column_for_aligned_line_comments(const Mode& mode,
                                                 Contents_Iterator start,
                                                 uint64_t end);

/// For each line in the region insert a line comment (`yes_space`) at `visual_column`.
/// If the line is empty then indents the line to `visual_column` then inserts `no_space`.
void insert_line_comments(Transaction* transaction,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          uint64_t visual_column,
                          cz::Str no_space,
                          cz::Str yes_space);

/// Convenience wrapper that calculates `visual_column` for you.
void insert_line_comments(Transaction* transaction,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          cz::Str no_space,
                          cz::Str yes_space);

}
