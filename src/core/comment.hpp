#pragma once

#include "core/buffer.hpp"
#include "core/contents.hpp"
#include "core/mode.hpp"
#include "core/transaction.hpp"
#include "core/window.hpp"

namespace mag {

/// Get the visual column that line comments should be inserted at
/// when trying to insert line comments for every line in the region.
///
/// In practice, this means the lowest visual column amongst the lines in the region.
/// Ignores empty lines, assuming that they will be padded to the resulting column.
uint64_t visual_column_for_aligned_line_comments(const Mode& mode,
                                                 Contents_Iterator start,
                                                 uint64_t end);

/// For each line in the region insert `comment_start` at `visual_column`.
///
/// If the line is empty then indents the line to `visual_column` then inserts `comment_start`.
///
/// If the line is not empty then inserts `comment_start` and then a space.  It will
/// also break tabs at and after `visual_column` such that the comments line up.
void insert_line_comments(Transaction* transaction,
                          uint64_t* offset,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          uint64_t visual_column,
                          cz::Str comment_start);

/// Convenience wrapper that calculates `visual_column` for you.
void insert_line_comments(Transaction* transaction,
                          uint64_t* offset,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          cz::Str comment_start);

/// Remove the line comments from the region.
///
/// Fixes the indentation of the lines that had tabs broken.
/// Handles lines that were empty and became indented.
void remove_line_comments(Transaction* transaction,
                          uint64_t* offset,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          cz::Str comment_start);

/// Helper routine to comment or uncomment bunch of regions / lines.
void generic_line_comment(Client* client,
                          Buffer* buffer,
                          Window_Unified* window,
                          cz::Str comment_start,
                          bool add);

}
