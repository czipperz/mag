#include "window_completion_commands.hpp"

#include "command_macros.hpp"
#include "commands.hpp"

namespace mag {
namespace basic {
namespace window_completion {

void command_finish_completion(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->completing) {
        return;
    }

    WITH_WINDOW_BUFFER(window);
    window->finish_completion(buffer);
}

void command_next_completion(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->completing) {
        return;
    }

    Completion_Filter_Context* context = &window->completion_cache.filter_context;
    if (context->selected + 1 >= context->results.len()) {
        return;
    }
    ++context->selected;
}

void command_previous_completion(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->completing) {
        return;
    }

    Completion_Filter_Context* context = &window->completion_cache.filter_context;
    if (context->selected == 0) {
        return;
    }
    --context->selected;
}

void command_completion_down_page(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->completing) {
        return;
    }

    Completion_Filter_Context* context = &window->completion_cache.filter_context;
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
    Window_Unified* window = source.client->selected_window();
    if (!window->completing) {
        return;
    }

    Completion_Filter_Context* context = &window->completion_cache.filter_context;
    if (context->selected < editor->theme.max_completion_results) {
        context->selected = 0;
        return;
    }
    context->selected -= editor->theme.max_completion_results;
}

void command_first_completion(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->completing) {
        return;
    }

    Completion_Filter_Context* context = &window->completion_cache.filter_context;
    context->selected = 0;
}

void command_last_completion(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->completing) {
        return;
    }

    Completion_Filter_Context* context = &window->completion_cache.filter_context;
    context->selected = context->results.len();
    if (context->selected > 0) {
        --context->selected;
    }
}

}
}
}
