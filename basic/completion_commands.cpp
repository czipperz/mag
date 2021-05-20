#include "completion_commands.hpp"

#include "command_macros.hpp"
#include "commands.hpp"

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

    size_t common_base;
    for (common_base = 0; common_base < query.len && common_base < value.len; ++common_base) {
        if (query[common_base] != value[common_base]) {
            break;
        }
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    bool removing = common_base < query.len;
    if (removing) {
        Edit remove;
        remove.value =
            SSOStr::as_duplicate(transaction.value_allocator(), query.slice_start(common_base));
        remove.position = common_base;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);
    }

    bool inserting = common_base < value.len;
    if (inserting) {
        Edit insert;
        insert.value =
            SSOStr::as_duplicate(transaction.value_allocator(), value.slice_start(common_base));
        insert.position = common_base;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();
}

void command_insert_completion_and_submit_mini_buffer(Editor* editor, Command_Source source) {
    command_insert_completion(editor, source);
    command_submit_mini_buffer(editor, source);
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
