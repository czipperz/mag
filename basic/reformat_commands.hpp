#pragma once

namespace cz {
struct Str;
}

namespace mag {
struct Buffer;
struct Contents_Iterator;
struct Editor;
struct Command_Source;

namespace basic {

/// Reformat at the iterator blocks of text where the first line starts with
/// `acceptable_start` and each consecutive line starts with `acceptable_continuation`.
/// `acceptable_continuation` can be left-padded with spaces.
///
/// Returns `true` if the patterns match.
///
/// If the range already matches the formatted output, no changes are made to the buffer.
bool reformat_at(Buffer* buffer,
                 Contents_Iterator iterator,
                 cz::Str acceptable_start,
                 cz::Str acceptable_continuation);

void command_reformat_paragraph(Editor* editor, Command_Source source);

}
}
