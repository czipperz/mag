#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace gnu_global {

struct Tag {
    /// Should be deallocated via `cz::heap_allocator()`.
    cz::String buffer;
    /// Null terminated and a slice of `buffer`.
    cz::Str file_name;
    uint64_t line;
};

/// Lookup a `query` to get a `Tag`.
///
/// If there is an error, returns a string describing the error.  On success, returns `nullptr`.
const char* lookup(const char* directory, cz::Str query, Tag* tag);

void command_lookup_at_point(Editor* editor, Command_Source source);

void command_lookup_prompt(Editor* editor, Command_Source source);

void command_complete_at_point(Editor* editor, Command_Source source);

}
}
