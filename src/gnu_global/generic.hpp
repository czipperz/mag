#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace tags {

enum Engine {
    GNU_GLOBAL,
    CTAGS,
};
bool pick_engine(cz::Str directory, Engine* engine);

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
const char* lookup_symbol(const char* directory,
                          cz::Str query,
                          cz::Vector<Tag>* tags,
                          cz::String* buffer);

/// Open a tag from the list of tags based on user input.
///
/// If there is only one tag then it is immediately opened.
/// If there are none then an error is shown to the user.
///
/// `query` is used to format the dialog's message.
///
/// Takes ownership of `tags` and `ba`.
void prompt_open_tags(Editor* editor,
                      Client* client,
                      cz::Vector<Tag> tags,
                      cz::Buffer_Array ba,
                      cz::Str query);

/// Chain `lookup_symbol` and `prompt_open_tags`.
void lookup_and_prompt(Editor* editor, Client* client, const char* directory, cz::Str query);

void command_lookup_at_point(Editor* editor, Command_Source source);
void command_lookup_prompt(Editor* editor, Command_Source source);
void command_move_mouse_and_lookup_at_point(Editor* editor, Command_Source source);

void command_complete_at_point(Editor* editor, Command_Source source);

void command_lookup_previous_command(Editor* editor, Command_Source source);

}
}
