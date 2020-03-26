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

    size_t query_offset = 0;
    const char* last_slash = results->query.rfind('/');
    if (last_slash) {
        query_offset = last_slash - results->query.start() + 1;
    }

    cz::Str value = results->results[results->selected];

    Transaction transaction;
    transaction.init(2, results->query.len() - query_offset + value.len);

    Edit remove;
    remove.value.init_duplicate(
        transaction.value_allocator(),
        {results->query.buffer() + query_offset, results->query.len() - query_offset});
    remove.position = query_offset;
    remove.flags = Edit::REMOVE;
    transaction.push(remove);

    Edit insert;
    insert.value.init_duplicate(transaction.value_allocator(), value);
    insert.position = query_offset;
    insert.flags = Edit::INSERT;
    transaction.push(insert);

    transaction.commit(buffer);
}

void command_next_completion(Editor* editor, Command_Source source) {
    Completion_Results* results = &source.client->mini_buffer_completion_results;
    if (results->selected + 1 >= results->results.len()) {
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

void command_first_completion(Editor* editor, Command_Source source) {
    Completion_Results* results = &source.client->mini_buffer_completion_results;
    results->selected = 0;
}

void command_last_completion(Editor* editor, Command_Source source) {
    Completion_Results* results = &source.client->mini_buffer_completion_results;
    results->selected = results->results.len();
    if (results->selected > 0) {
        --results->selected;
    }
}

}
}
