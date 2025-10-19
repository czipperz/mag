#include "macro_commands.hpp"

#include "basic/search_commands.hpp"
#include "core/client.hpp"
#include "core/command_macros.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_start_recording_macro);
void command_start_recording_macro(Editor* editor, Command_Source source) {
    source.client->show_message("Start recording macro");
    source.client->record_key_presses = true;
    source.client->macro_key_chain.len = 0;
    source.client->key_chain.remove_range(0, source.client->key_chain_offset);
    source.client->key_chain_offset = 0;
}

REGISTER_COMMAND(command_stop_recording_macro);
void command_stop_recording_macro(Editor* editor, Command_Source source) {
    if (!source.client->record_key_presses) {
        source.client->show_message("No macro to stop recording");
        return;
    }
    source.client->show_message("Stop recording macro");
    size_t end = source.client->key_chain_offset - source.keys.len;
    source.client->macro_key_chain.reserve(cz::heap_allocator(), end);
    source.client->macro_key_chain.append({source.client->key_chain.start(), end});
    source.client->key_chain.len = 0;
    source.client->key_chain_offset = 0;
    source.client->record_key_presses = false;
}

REGISTER_COMMAND(command_run_macro);
void command_run_macro(Editor* editor, Command_Source source) {
    source.client->key_chain.reserve(cz::heap_allocator(), source.client->macro_key_chain.len);
    source.client->key_chain.insert_slice(source.client->key_chain_offset,
                                          source.client->macro_key_chain);
}

REGISTER_COMMAND(command_run_macro_forall_lines_in_search);
void command_run_macro_forall_lines_in_search(Editor* editor, Command_Source source) {
    bool recurse = true;

    // If cursor in other window is at eob then stop.
    {
        Window_Unified* selected = source.client->selected_normal_window;
        if (!selected->parent)
            return;  // invalid

        Window* other_base = (selected->parent->first == selected ? selected->parent->second
                                                                  : selected->parent->first);
        if (other_base->tag != Window::UNIFIED)
            return;  // invalid

        Window_Unified* other = (Window_Unified*)other_base;
        WITH_CONST_WINDOW_BUFFER(other, source.client);

        // In multi-cursor mode or matching mode, the cursor doesn't ever point to
        // the end of the buffer.  So instead we detect if we're on the last cursor
        // or match.  If so then run the command one more time and then stop.
        if (other->cursors.len > 1) {
            recurse = (other->selected_cursor + 1 < other->cursors.len);
        } else if (other->show_marks) {
            const Cursor& cursor = other->cursors[other->selected_cursor];
            Contents_Iterator it = buffer->contents.iterator_at(cursor.start());
            recurse = search_forward_slice(buffer, &it, cursor.end());
        } else {
            if (other->cursors[0].point == buffer->contents.len)
                return;
            recurse = true;
        }
    }

    // Push the macro.
    source.client->key_chain.reserve(cz::heap_allocator(), source.client->macro_key_chain.len);
    source.client->key_chain.insert_slice(source.client->key_chain_offset,
                                          source.client->macro_key_chain);

    if (recurse) {
        // Reschedule this command.
        source.client->key_chain.reserve(cz::heap_allocator(), source.keys.len);
        source.client->key_chain.insert_slice(
            source.client->key_chain_offset + source.client->macro_key_chain.len, source.keys);
    }
}

REGISTER_COMMAND(command_print_macro);
void command_print_macro(Editor* editor, Command_Source source) {
    cz::String message = {};
    CZ_DEFER(message.drop(cz::heap_allocator()));
    message.reserve(cz::heap_allocator(), 256);
    message.append("Macro: ");
    stringify_keys(cz::heap_allocator(), &message, source.client->macro_key_chain);
    source.client->show_message(message);
}

}
}
