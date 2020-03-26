#include "completion_commands.hpp"

#include "command_macros.hpp"

namespace mag {
namespace basic {

void command_insert_completion(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->mini_buffer_window();
    WITH_WINDOW_BUFFER(window);
    Completion_Results* results = &source.client->mini_buffer_completion_results;
    if (results->selected >= results->results.len()) {
        return;
    }

    cz::Str value = results->results[results->selected];

    Transaction transaction;
    transaction.init(2, buffer->contents.len + value.len);

    Edit remove;
    remove.value = buffer->contents.slice(transaction.value_allocator(), buffer->contents.start(),
                                          buffer->contents.len);
    remove.position = 0;
    remove.flags = Edit::REMOVE;
    transaction.push(remove);

    Edit insert;
    insert.value.init_duplicate(transaction.value_allocator(), value);
    insert.position = 0;
    insert.flags = Edit::INSERT;
    transaction.push(insert);

    transaction.commit(buffer);
}

void command_next_completion(Editor* editor, Command_Source source) {
    Completion_Results* results = &source.client->mini_buffer_completion_results;
    if (results->selected >= results->results.len()) {
        return;
    }
    ++results->selected;
}

void command_previous_completion(Editor* editor, Command_Source source) {
    Completion_Results* results = &source.client->mini_buffer_completion_results;
    if (results->selected == 0) {
        return;
    }
    --results->selected;
}

}
}
