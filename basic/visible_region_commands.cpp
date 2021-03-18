#include "visible_region_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void command_center_in_window(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    center_in_window(window, buffer->contents.iterator_at(window->cursors[0].point));
}

void command_goto_center_of_window(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    window->cursors[0].point = center_of_window(window, &buffer->contents).position;
}

void command_up_page(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    kill_extra_cursors(window, source.client);
    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    compute_visible_start(window, &it);
    window->start_position = it.position;
    forward_line(buffer->mode, &it);
    window->cursors[0].point = it.position;
}

void command_down_page(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    kill_extra_cursors(window, source.client);
    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    compute_visible_end(window, &it);
    forward_char(&it);
    window->start_position = it.position;
    forward_line(buffer->mode, &it);
    window->cursors[0].point = it.position;
}

static void scroll_down(Editor* editor, Command_Source source, size_t num) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    for (size_t i = 0; i < num; ++i) {
        forward_line(buffer->mode, &it);
    }

    window->start_position = it.position;

    forward_line(buffer->mode, &it);
    if (window->cursors[0].point < it.position) {
        kill_extra_cursors(window, source.client);
        window->cursors[0].point = it.position;
    }
}

static void scroll_up(Editor* editor, Command_Source source, size_t num) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->start_position);
    for (size_t i = 0; i < num; ++i) {
        backward_line(buffer->mode, &it);
    }

    window->start_position = it.position;

    compute_visible_end(window, &it);
    if (window->cursors[0].point > it.position) {
        kill_extra_cursors(window, source.client);
        window->cursors[0].point = it.position;
    }
}

void command_scroll_down(Editor* editor, Command_Source source) {
    scroll_down(editor, source, 4);
}
void command_scroll_up(Editor* editor, Command_Source source) {
    scroll_up(editor, source, 4);
}

void command_scroll_down_one(Editor* editor, Command_Source source) {
    scroll_down(editor, source, 1);
}
void command_scroll_up_one(Editor* editor, Command_Source source) {
    scroll_up(editor, source, 1);
}

}
}
