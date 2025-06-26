#pragma once

#include "core/client.hpp"
#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void reset_mode(Editor* editor, Buffer* buffer);
void reset_mode_as_if(Editor* editor,
                      Buffer* buffer,
                      cz::Str name,
                      cz::Str directory,
                      Buffer::Type type);
/// Combines `reset_mode_as_if` with `parse_rendered_buffer_name`
/// and returns false on parse failure.
bool reset_mode_as_if_named(Editor* editor, Buffer* buffer, cz::Str rendered_buffer_name);

void command_open_file(Editor* editor, Command_Source source);
void command_open_file_full_path(Editor* editor, Command_Source source);
void command_save_file(Editor* editor, Command_Source source);

void command_switch_buffer(Editor* editor, Command_Source source);
void command_kill_buffer(Editor* editor, Command_Source source);
void command_kill_buffers_in_folder(Editor* editor, Command_Source source);
void command_delete_file_and_kill_buffer(Editor* editor, Command_Source source);

void command_rename_buffer(Editor* editor, Command_Source source);
void command_save_buffer_to(Editor* editor, Command_Source source);
void command_pretend_rename_buffer(Editor* editor, Command_Source source);

void command_diff_buffer_file_against(Editor* editor, Command_Source source);
void command_diff_buffer_contents_against(Editor* editor, Command_Source source);

void command_reload_buffer(Editor* editor, Command_Source source);

}
}
