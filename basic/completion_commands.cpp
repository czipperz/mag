#include "completion_commands.hpp"

#include "command_macros.hpp"

namespace mag {
namespace basic {

void command_insert_completion(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->mini_buffer_window();
    WITH_WINDOW_BUFFER(window);
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    cz::Str query = source.client->mini_buffer_completion_cache.engine_context.query;
    if (context->selected >= context->results.len()) {
        return;
    }

    cz::Str value = context->results[context->selected];

    Transaction transaction;
    transaction.init(2, query.len + value.len);

    Edit remove;
    remove.value.init_duplicate(transaction.value_allocator(), query);
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
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected + 1 >= context->results.len()) {
        return;
    }
    ++context->selected;
}

void command_previous_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected == 0) {
        return;
    }
    --context->selected;
}

void command_completion_down_page(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected + editor->theme.max_completion_results >= context->results.len()) {
        context->selected = context->results.len();
        if (context->selected > 0) {
            --context->selected;
        }
        return;
    }
    context->selected += editor->theme.max_completion_results;
}

void command_completion_up_page(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected < editor->theme.max_completion_results) {
        context->selected = 0;
        return;
    }
    context->selected -= editor->theme.max_completion_results;
}

void command_first_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    context->selected = 0;
}

void command_last_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    context->selected = context->results.len();
    if (context->selected > 0) {
        --context->selected;
    }
}

}
}
