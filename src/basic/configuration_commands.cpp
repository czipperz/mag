#include "configuration_commands.hpp"

#include <cz/format.hpp>
#include <cz/heap_string.hpp>
#include <cz/parse.hpp>
#include "core/command_macros.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_toggle_read_only);
void command_toggle_read_only(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->read_only = !buffer->read_only;
}

REGISTER_COMMAND(command_toggle_pinned);
void command_toggle_pinned(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    window->pinned = !window->pinned;
}

REGISTER_COMMAND(command_toggle_draw_line_numbers);
void command_toggle_draw_line_numbers(Editor* editor, Command_Source source) {
    editor->theme.draw_line_numbers = !editor->theme.draw_line_numbers;
}

REGISTER_COMMAND(command_toggle_line_feed);
void command_toggle_line_feed(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->use_carriage_returns = !buffer->use_carriage_returns;
}

REGISTER_COMMAND(command_toggle_render_bucket_boundaries);
void command_toggle_render_bucket_boundaries(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->mode.render_bucket_boundaries = !buffer->mode.render_bucket_boundaries;
}

REGISTER_COMMAND(command_toggle_use_tabs);
void command_toggle_use_tabs(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->mode.use_tabs = !buffer->mode.use_tabs;
}

REGISTER_COMMAND(command_toggle_animated_scrolling);
void command_toggle_animated_scrolling(Editor* editor, Command_Source source) {
    editor->theme.allow_animated_scrolling = !editor->theme.allow_animated_scrolling;
}

REGISTER_COMMAND(command_toggle_wrap_long_lines);
void command_toggle_wrap_long_lines(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->mode.wrap_long_lines = !buffer->mode.wrap_long_lines;
}

REGISTER_COMMAND(command_toggle_insert_replace);
void command_toggle_insert_replace(Editor* editor, Command_Source source) {
    editor->theme.insert_replace = !editor->theme.insert_replace;
    if (editor->theme.insert_replace) {
        source.client->show_message("Insert replace on");
    } else {
        source.client->show_message("Insert replace off");
    }
}

static void command_configure_callback(Editor* editor, Client* client, cz::Str query, void* _data) {
    if (query == "animated scrolling") {
        editor->theme.allow_animated_scrolling = !editor->theme.allow_animated_scrolling;
    } else if (query == "buffer indent width") {
        cz::Heap_String prompt = {};
        CZ_DEFER(prompt.drop());
        {
            WITH_CONST_SELECTED_BUFFER(client);
            prompt = cz::format("Set indent width to (", buffer->mode.indent_width, "): ");
        }

        Dialog dialog = {};
        dialog.prompt = prompt;
        dialog.response_callback = [](Editor* editor, Client* client, cz::Str str, void*) {
            WITH_SELECTED_BUFFER(client);
            uint32_t value;
            if (cz::parse(str, &value) <= 0 || value == 0) {
                client->show_message("Invalid indent width");
                return;
            }
            buffer->mode.indent_width = value;
        };
        client->show_dialog(dialog);
    } else if (query == "buffer tab width") {
        cz::Heap_String prompt = {};
        CZ_DEFER(prompt.drop());
        {
            WITH_CONST_SELECTED_BUFFER(client);
            prompt = cz::format("Set tab width to (", buffer->mode.tab_width, "): ");
        }

        Dialog dialog = {};
        dialog.prompt = prompt;
        dialog.response_callback = [](Editor* editor, Client* client, cz::Str str, void*) {
            WITH_SELECTED_BUFFER(client);
            uint32_t value;
            if (cz::parse(str, &value) <= 0 || value == 0) {
                client->show_message("Invalid tab width");
                return;
            }
            buffer->mode.tab_width = value;
        };
        client->show_dialog(dialog);
    } else if (query == "buffer use tabs") {
        bool use_tabs;
        {
            WITH_SELECTED_BUFFER(client);
            use_tabs = buffer->mode.use_tabs = !buffer->mode.use_tabs;
        }
        if (use_tabs) {
            client->show_message("Buffer now uses tabs");
        } else {
            client->show_message("Buffer now does not use tabs");
        }
    } else if (query == "buffer tabs for alignment") {
        bool tabs_for_alignment;
        {
            WITH_SELECTED_BUFFER(client);
            tabs_for_alignment = buffer->mode.tabs_for_alignment = !buffer->mode.tabs_for_alignment;
        }
        if (tabs_for_alignment) {
            client->show_message("Buffer now uses tabs for alignment");
        } else {
            client->show_message("Buffer now does not use tabs for alignment");
        }
    } else if (query == "buffer line feed crlf") {
        WITH_SELECTED_BUFFER(client);
        buffer->use_carriage_returns = !buffer->use_carriage_returns;
    } else if (query == "buffer pinned") {
        Window_Unified* window = client->selected_window();
        window->pinned = !window->pinned;
    } else if (query == "buffer preferred column") {
        cz::Heap_String prompt = {};
        CZ_DEFER(prompt.drop());
        {
            WITH_CONST_SELECTED_BUFFER(client);
            prompt = cz::format("Set preferred column to (", buffer->mode.preferred_column, "): ");
        }

        Dialog dialog = {};
        dialog.prompt = prompt;
        dialog.response_callback = [](Editor* editor, Client* client, cz::Str str, void*) {
            WITH_SELECTED_BUFFER(client);
            int64_t value;
            if (cz::parse(str, &value) <= 0) {
                client->show_message("Invalid preferred column");
                return;
            }
            if (value <= 0)
                value = -1;
            buffer->mode.preferred_column = (uint64_t)value;
        };
        client->show_dialog(dialog);
    } else if (query == "buffer read only") {
        WITH_SELECTED_BUFFER(client);
        buffer->read_only = !buffer->read_only;
    } else if (query == "buffer render bucket boundaries") {
        WITH_SELECTED_BUFFER(client);
        buffer->mode.render_bucket_boundaries = !buffer->mode.render_bucket_boundaries;
    } else if (query == "buffer wrap long lines") {
        WITH_SELECTED_BUFFER(client);
        buffer->mode.wrap_long_lines = !buffer->mode.wrap_long_lines;
    } else if (query == "draw line numbers") {
        editor->theme.draw_line_numbers = !editor->theme.draw_line_numbers;
    } else if (query == "insert replace") {
        editor->theme.insert_replace = !editor->theme.insert_replace;
        if (editor->theme.insert_replace) {
            client->show_message("Insert replace on");
        } else {
            client->show_message("Insert replace off");
        }
    } else if (query == "font size") {
        cz::Heap_String prompt = cz::format("Set font size to (", editor->theme.font_size, "): ");
        CZ_DEFER(prompt.drop());

        Dialog dialog = {};
        dialog.prompt = prompt;
        dialog.response_callback = [](Editor* editor, Client* client, cz::Str str, void*) {
            uint32_t value;
            if (cz::parse(str, &value) <= 0 || value == 0) {
                client->show_message("Invalid font size (only ints for now)");
                return;
            }
            editor->theme.font_size = value;
        };
        client->show_dialog(dialog);
    } else {
        client->show_message("Invalid configuration variable");
    }
}

static bool configurations_completion_engine(Editor* editor,
                                             Completion_Engine_Context* context,
                                             bool is_initial_frame) {
    if (context->results.len != 0) {
        return false;
    }

    context->results.reserve(13);
    context->results.push("buffer indent width");
    context->results.push("buffer tab width");
    context->results.push("buffer use tabs");
    context->results.push("buffer tabs for alignment");
    context->results.push("buffer line feed crlf");
    context->results.push("buffer pinned");
    context->results.push("buffer preferred column");
    context->results.push("buffer read only");
    context->results.push("buffer render bucket boundaries");
    context->results.push("buffer wrap long lines");
    context->results.push("animated scrolling");
    context->results.push("draw line numbers");
    context->results.push("insert replace");
    context->results.push("font size");
    return true;
}

REGISTER_COMMAND(command_configure);
void command_configure(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Configuration to change: ";
    dialog.completion_engine = configurations_completion_engine;
    dialog.response_callback = command_configure_callback;
    source.client->show_dialog(dialog);
}

}
}
