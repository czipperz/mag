#pragma once

#include <cz/slice.hpp>

namespace cz {
struct Str;
}

namespace mag {
struct Buffer;
struct Contents_Iterator;
struct Editor;
struct Command_Source;

namespace basic {

extern bool sentences_start_with_two_spaces;

/// Reformat at the iterator blocks of text where the first line starts with
/// `acceptable_start` and each consecutive line starts with `acceptable_continuation`.
///
/// `acceptable_continuation` can be left-padded with spaces.
///
/// A line that starts with `acceptable_start` or `acceptable_continuation`
/// and then a pattern from `rejected_patterns` will not be reformatted.
///
/// Returns `true` if the patterns match.
///
/// If the range already matches the formatted output, no changes are made to the buffer.
bool reformat_at(Buffer* buffer,
                 Contents_Iterator iterator,
                 cz::Str acceptable_start,
                 cz::Str acceptable_continuation,
                 cz::Slice<cz::Str> rejected_patterns = {});

void command_reformat_paragraph(Editor* editor, Command_Source source);
void command_reformat_comment_hash(Editor* editor, Command_Source source);

}
}
