#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace gnu_global {

struct Reference {
    /// Should be deallocated via `cz::heap_allocator()`.
    cz::String buffer;
    /// Null terminated and a slice of `buffer`.
    cz::Str file_name;
    uint64_t line;
};

/// Lookup a `tag` to get a `Reference`.
///
/// If there is an error, returns a string describing the error.  On success, returns `nullptr`.
const char* lookup(const char* directory, const char* tag, Reference* reference);

void command_lookup(Editor* editor, Command_Source source);

}
}
