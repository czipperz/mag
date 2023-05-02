#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace prose {

/// If the path matches an entry in `alternate_path_1` it is replaced with the corresponding
/// entry in `alternate_path_2`.  If an entry doesn't match the entry in `alternate_path_1`
/// then `alternate_path_2` is tested (and replaced with `alternate_path_1`.
///
/// Multiple replacements may be applied.
extern cz::Str alternate_path_1[];
extern cz::Str alternate_path_2[];
extern size_t alternate_path_len;

/// If the path matches an extension in `alternate_extensions_1` then we try to test all alternate
/// extensions in `alternate_extensions_2`.  If exactly one of these paths is a file that exists on
/// disk then it is chosen.  Otherwise the extension at the same index in `alternate_extensions_2`
/// is preferred and then the lowest index.  If no extensions in `alternate_extensions_1` match
/// then the extensions in `alternate_extensions_2` are tested.
///
/// At most one replacement will be applied.
extern cz::Str alternate_extensions_1[];
extern cz::Str alternate_extensions_2[];
extern size_t alternate_extensions_len;

/// Find the alternate file.
/// Returns 0 if `path` doesn't match any rules.  Note that `path` may be changed by this function in this case!
/// Returns 1 if a match is guessed.  This happens when creating a new alternate file.
/// Returns 2 if an exact match is found on disk.
int find_alternate_file(cz::String* path);

void command_alternate(Editor* editor, Command_Source source);

}
}
