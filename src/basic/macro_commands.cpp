#include "macro_commands.hpp"

#include "core/client.hpp"
#include "core/command_macros.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_start_recording_macro);
void command_start_recording_macro(Editor* editor, Command_Source source) {
    source.client->show_message("Start recording macro");
    source.client->record_key_presses = true;
    source.client->macro_key_chain.len = 0;
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
        Window_Unified* window = source.client->selected_normal_window;
        if (!window->parent)
            return;  // invalid

        Window* other =
            (window->parent->first == window ? window->parent->second : window->parent->first);
        if (other->tag != Window::UNIFIED)
            return;  // invalid

        Window_Unified* other2 = (Window_Unified*)other;
        WITH_CONST_WINDOW_BUFFER(other2);
        uint64_t point = other2->cursors[0].point;
        if (point == buffer->contents.len)
            return;  // done

        // In multi-cursor mode, the cursor doesn't ever point to the end
        // of the buffer.  So instead we detect if we're on the last
        // cursor.  If so then run the command one more time and then stop.
        recurse = (other2->cursors.len == 1 || other2->selected_cursor + 1 < other2->cursors.len);
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
    message.append("Macro:");
    const auto is_basic = [](const Key& key) {
        return key.modifiers == 0 && key.code <= UCHAR_MAX && cz::is_graph((char)key.code);
    };
    for (size_t i = 0; i < source.client->macro_key_chain.len; ++i) {
        if (i >= 1 && is_basic(source.client->macro_key_chain[i - 1]) &&
            is_basic(source.client->macro_key_chain[i])) {
            message.reserve(cz::heap_allocator(), 1);
            message.push(source.client->macro_key_chain[i].code);
            continue;
        }
        message.reserve(cz::heap_allocator(), 1 + stringify_key_max_size);
        message.push(' ');
        stringify_key(&message, source.client->macro_key_chain[i]);
    }
    source.client->show_message(message);
}

}
}
