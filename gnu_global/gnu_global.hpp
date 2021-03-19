#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace gnu_global {

struct Tag {
    /// Null terminated.
    cz::Str file_name;
    uint64_t line;
};

/// Lookup a `query` to get a `Tag`.
///
/// If there is an error, returns a string describing the error.  On success,
/// returns `nullptr`.  Note that having no results is counted as success.
///
/// `tags` is a heap allocated vector of tags.  The `file_name`
/// fields of `Tag` belong to `buffer` which is also heap allocated.
const char* lookup(const char* directory, cz::Str query, cz::Vector<Tag>* tags, cz::String* buffer);

/// Open a tag from the list of tags based on user input.
///
/// If there is only one tag then it is immediately opened.
/// If there are none then an error is shown to the user.
///
/// Cleans up `tags` and `buffer`.
void prompt_open_tags(Editor* editor, Client* client, cz::Vector<Tag> tags, cz::String buffer);

void command_lookup_at_point(Editor* editor, Command_Source source);

void command_lookup_prompt(Editor* editor, Command_Source source);

void command_complete_at_point(Editor* editor, Command_Source source);

bool completion_engine(Editor* editor, Completion_Engine_Context* context, bool is_initial_frame);

}
}
