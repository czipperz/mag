#include "macro_commands.hpp"

#include "client.hpp"

namespace mag {
namespace basic {

void command_start_recording_macro(Editor* editor, Command_Source source) {
    source.client->show_message("Start recording macro");
    source.client->record_key_presses = true;
    source.client->macro_key_chain.len = 0;
}

void command_stop_recording_macro(Editor* editor, Command_Source source) {
    source.client->show_message("Stop recording macro");
    size_t end = source.client->key_chain_offset - source.keys.len;
    source.client->macro_key_chain.reserve(cz::heap_allocator(), end);
    source.client->macro_key_chain.append({source.client->key_chain.start(), end});
    source.client->record_key_presses = false;
}

void command_run_macro(Editor* editor, Command_Source source) {
    source.client->key_chain.reserve(cz::heap_allocator(), source.client->macro_key_chain.len);
    source.client->key_chain.insert_slice(source.client->key_chain_offset,
                                          source.client->macro_key_chain);
}

}
}
