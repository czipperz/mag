#include "commands.hpp"

#include <cz/defer.hpp>
#include <cz/option.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "movement.hpp"
#include "transaction.hpp"

namespace mag {

void command_set_mark(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {
            buffer->cursors[i].mark = buffer->cursors[i].point;
        }
        buffer->show_marks = true;
    });
}

void command_delete_region(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION({
        transaction.reserve(buffer->cursors.len());

        size_t offset = 0;
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {
            uint64_t start = buffer->cursors[i].start();
            uint64_t end = buffer->cursors[i].end();

            Edit edit;
            edit.value = buffer->contents.slice(buffer->edit_buffer.allocator(), start, end);
            edit.position = start - offset;
            offset += end - start;
            edit.is_insert = false;
            transaction.push(edit);
        }

        buffer->show_marks = false;
    }));
}

void command_swap_mark_point(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {
            cz::swap(buffer->cursors[i].point, buffer->cursors[i].mark);
        }
    });
}

void command_forward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(forward_char));
}

void command_backward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(backward_char));
}

void command_forward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(forward_word));
}

void command_backward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(backward_word));
}

void command_forward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(forward_line));
}

void command_backward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(backward_line));
}

void command_end_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(end_of_line));
}

void command_start_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(start_of_line));
}

void command_shift_line_forward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        cz::Vector<uint64_t> cursor_positions = {};
        CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
        cursor_positions.reserve(cz::heap_allocator(), buffer->cursors.len());

        WITH_TRANSACTION({
            transaction.reserve(buffer->cursors.len() * 3);

            for (size_t i = 0; i < buffer->cursors.len(); ++i) {
                uint64_t start = start_of_line(buffer, buffer->cursors[i].point);
                uint64_t end = end_of_line(buffer, buffer->cursors[i].point);
                uint64_t column = buffer->cursors[i].point - start;
                uint64_t insertion_point = end_of_line(buffer, forward_char(buffer, end));
                if (insertion_point == end) {
                    continue;
                }

                // abc\ndef\nghi\njkl\n
                //      [  )    ^
                //  start  end  ^
                //       insertion_point

                cursor_positions.push(insertion_point - (end - start) + column);

                if (insertion_point < buffer->contents.len()) {
                    // abc\ndef\nghi\njkl\n
                    //      [    )    ^
                    //  start    end  ^
                    //         insertion_point
                    ++insertion_point;
                    ++end;
                } else if (start > 0) {
                    // abc\ndef\nghi
                    //    [    )    ^
                    //  start end   ^
                    //       insertion_point
                    --start;
                } else {
                    //    abc\ndef
                    //    [    )  ^
                    // start  end ^
                    //     insertion_point
                    ++end;

                    SSOStr value =
                        buffer->contents.slice(buffer->edit_buffer.allocator(), start, end);

                    Edit edit_insert;
                    cz::Str value_str = value.as_str();
                    edit_insert.value.init_from_constant({value_str.buffer, value_str.len - 1});
                    edit_insert.position = insertion_point;
                    edit_insert.is_insert = true;
                    transaction.push(edit_insert);

                    Edit edit_insert_newline;
                    edit_insert_newline.value.init_char('\n');
                    edit_insert_newline.position = insertion_point;
                    edit_insert_newline.is_insert = true;
                    transaction.push(edit_insert_newline);

                    Edit edit_delete;
                    edit_delete.value = value;
                    edit_delete.position = start;
                    edit_delete.is_insert = false;
                    transaction.push(edit_delete);
                    continue;
                }

                SSOStr value = buffer->contents.slice(buffer->edit_buffer.allocator(), start, end);

                Edit edit_insert;
                edit_insert.value = value;
                edit_insert.position = insertion_point;
                edit_insert.is_insert = true;
                transaction.push(edit_insert);

                Edit edit_delete;
                edit_delete.value = value;
                edit_delete.position = start;
                edit_delete.is_insert = false;
                transaction.push(edit_delete);
            }
        });

        for (size_t i = 0; i < buffer->cursors.len(); ++i) {
            buffer->cursors[i].point = cursor_positions[i];
        }
    });
}

void command_shift_line_backward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        cz::Vector<uint64_t> cursor_positions = {};
        CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
        cursor_positions.reserve(cz::heap_allocator(), buffer->cursors.len());

        WITH_TRANSACTION({
            transaction.reserve(buffer->cursors.len() * 3);

            for (size_t i = 0; i < buffer->cursors.len(); ++i) {
                uint64_t start = start_of_line(buffer, buffer->cursors[i].point);
                uint64_t end = end_of_line(buffer, buffer->cursors[i].point);
                uint64_t column = buffer->cursors[i].point - start;
                uint64_t insertion_point = backward_line(buffer, start);
                if (insertion_point == start) {
                    continue;
                }

                // abc\ndef\nghi\njkl\n
                //      ^    [  )
                //      ^ start end
                // insertion_point

                cursor_positions.push(insertion_point + column);

                if (end < buffer->contents.len()) {
                    // Normal case: pick up newline after end.
                    //
                    //   abc\ndef\nghi\njkl\n
                    //        ^    [    )
                    //        ^  start end
                    // insertion_point
                    ++end;
                } else if (insertion_point > 0) {
                    // We don't have the line after us.
                    //
                    // abc\ndef\nghi
                    //    ^    [    )
                    //    ^ start   end
                    // insertion_point
                    --start;
                    --insertion_point;
                } else {
                    // We don't have a line after us or before us.
                    //
                    // abc\ndef
                    //      [  )
                    SSOStr value =
                        buffer->contents.slice(buffer->edit_buffer.allocator(), start - 1, end);

                    Edit edit_delete;
                    edit_delete.value = value;
                    edit_delete.position = start - 1;
                    edit_delete.is_insert = false;
                    transaction.push(edit_delete);

                    Edit edit_insert;
                    cz::Str value_str = value.as_str();
                    edit_insert.value.init_from_constant({value_str.buffer + 1, value_str.len - 1});
                    edit_insert.position = insertion_point;
                    edit_insert.is_insert = true;
                    transaction.push(edit_insert);

                    Edit edit_insert_newline;
                    edit_insert_newline.value.init_char('\n');
                    edit_insert_newline.position = value_str.len - 1;
                    edit_insert_newline.is_insert = true;
                    transaction.push(edit_insert_newline);
                    continue;
                }

                SSOStr value = buffer->contents.slice(buffer->edit_buffer.allocator(), start, end);

                Edit edit_delete;
                edit_delete.value = value;
                edit_delete.position = start;
                edit_delete.is_insert = false;
                transaction.push(edit_delete);

                Edit edit_insert;
                edit_insert.value = value;
                edit_insert.position = insertion_point;
                edit_insert.is_insert = true;
                transaction.push(edit_insert);
            }
        });

        for (size_t i = 0; i < buffer->cursors.len(); ++i) {
            buffer->cursors[i].point = cursor_positions[i];
        }
    });
}

void command_delete_backward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION(DELETE_BACKWARD(backward_char)));
}

void command_delete_forward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION(DELETE_FORWARD(forward_char)));
}

void command_delete_backward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION(DELETE_BACKWARD(backward_word)));
}

void command_delete_forward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION(DELETE_FORWARD(forward_word)));
}

void command_undo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(buffer->undo());
}

void command_redo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(buffer->redo());
}

void command_stop_action(Editor* editor, Command_Source source) {
    bool done = false;
    WITH_SELECTED_BUFFER({
        if (!done && buffer->show_marks) {
            buffer->show_marks = false;
            done = true;
        }

        if (!done && buffer->cursors.len() > 1) {
            buffer->cursors.set_len(1);
            done = true;
        }
    });

    if (!done && source.client->selected_buffer_id() == source.client->mini_buffer_id()) {
        source.client->hide_mini_buffer();
        done = true;
    }

    Message message = {};
    message.tag = Message::SHOW;
    message.text = "Quit";
    source.client->show_message(message);
}

void command_quit(Editor* editor, Command_Source source) {
    source.client->queue_quit = true;
}

static void create_cursor_forward_line(Buffer* buffer) {
    CZ_DEBUG_ASSERT(buffer->cursors.len() >= 1);
    uint64_t last_point = buffer->cursors.last().point;
    uint64_t new_last_point = forward_line(buffer, last_point);
    if (new_last_point != last_point) {
        Cursor cursor;
        cursor.point = new_last_point;
        cursor.mark = cursor.point;

        buffer->cursors.reserve(cz::heap_allocator(), 1);
        buffer->cursors.push(cursor);
    }
}

void command_create_cursor_forward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(create_cursor_forward_line(buffer));
}

static void create_cursor_backward_line(Buffer* buffer) {
    CZ_DEBUG_ASSERT(buffer->cursors.len() >= 1);
    uint64_t first_point = buffer->cursors[0].point;
    uint64_t new_first_point = backward_line(buffer, first_point);
    if (new_first_point != first_point) {
        Cursor cursor;
        cursor.point = new_first_point;
        cursor.mark = cursor.point;

        buffer->cursors.reserve(cz::heap_allocator(), 1);
        buffer->cursors.insert(0, cursor);
    }
}

void command_create_cursor_backward_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(create_cursor_backward_line(buffer));
}

static cz::Option<uint64_t> search_forward(Buffer* buffer, uint64_t index, cz::Str query) {
    for (; index + query.len < buffer->contents.len(); ++index) {
        size_t q;
        for (q = 0; q < query.len; ++q) {
            if (buffer->contents[index + q] != query[q]) {
                break;
            }
        }

        if (q == query.len) {
            return index;
        }
    }

    return {};
}

static cz::Option<uint64_t> search_forward_slice(Buffer* buffer, uint64_t start, uint64_t end) {
    if (start == buffer->contents.len()) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));
    return search_forward(buffer, start + 1, slice.as_str());
}

#define SEARCH_SLICE_THEN(FUNC, THEN)                                 \
    do {                                                              \
        uint64_t start = buffer->cursors[c].start();                  \
        uint64_t end = buffer->cursors[c].end();                      \
        cz::Option<uint64_t> new_start = FUNC(buffer, start, end);    \
        if (new_start.is_present) {                                   \
            Cursor new_cursor;                                        \
            new_cursor.point = new_start.value;                       \
            new_cursor.mark = new_start.value + end - start;          \
            if (buffer->cursors[c].point > buffer->cursors[c].mark) { \
                cz::swap(new_cursor.point, new_cursor.mark);          \
            }                                                         \
            THEN;                                                     \
        }                                                             \
    } while (0)

#define SEARCH_QUERY_THEN(FUNC, THEN)                                \
    do {                                                             \
        uint64_t start = buffer->cursors[c].start();                 \
        cz::Option<uint64_t> new_start = FUNC(buffer, start, query); \
        if (new_start.is_present) {                                  \
            Cursor new_cursor;                                       \
            new_cursor.point = new_start.value + query.len();        \
            new_cursor.mark = new_start.value;                       \
            THEN;                                                    \
        }                                                            \
    } while (0)

#define SEARCH_QUERY_SET_CURSOR(FUNC) SEARCH_QUERY_THEN(FUNC, buffer->cursors[c] = new_cursor)

static void create_cursor_forward_search(Buffer* buffer) {
    CZ_DEBUG_ASSERT(buffer->cursors.len() >= 1);
    size_t c = buffer->cursors.len() - 1;
    SEARCH_SLICE_THEN(search_forward_slice, {
        buffer->cursors.reserve(cz::heap_allocator(), 1);
        buffer->cursors.push(new_cursor);
    });
}

void command_create_cursor_forward_search(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(create_cursor_forward_search(buffer));
}

static cz::Option<uint64_t> search_backward(Buffer* buffer, uint64_t index, cz::Str query) {
    if (query.len > buffer->contents.len()) {
        return {};
    }
    index = cz::min(index, buffer->contents.len() - query.len);

    while (index-- > 0) {
        size_t q;
        for (q = 0; q < query.len; ++q) {
            if (buffer->contents[index + q] != query[q]) {
                break;
            }
        }

        if (q == query.len) {
            return index;
        }
    }

    return {};
}

static cz::Option<uint64_t> search_backward_slice(Buffer* buffer, uint64_t start, uint64_t end) {
    if (start == 0) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));
    return search_backward(buffer, start - 1, slice.as_str());
}

static void create_cursor_backward_search(Buffer* buffer) {
    CZ_DEBUG_ASSERT(buffer->cursors.len() >= 1);
    size_t c = buffer->cursors.len() - 1;
    SEARCH_SLICE_THEN(search_backward_slice, {
        buffer->cursors.reserve(cz::heap_allocator(), 1);
        buffer->cursors.insert(0, new_cursor);
    });
}

void command_create_cursor_backward_search(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(create_cursor_backward_search(buffer));
}

void command_create_cursor_forward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        if (buffer->show_marks) {
            create_cursor_forward_search(buffer);
        } else {
            create_cursor_forward_line(buffer);
        }
    });
}

void command_create_cursor_backward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        if (buffer->show_marks) {
            create_cursor_backward_search(buffer);
        } else {
            create_cursor_backward_line(buffer);
        }
    });
}

static void command_search_forward_callback(Editor* editor,
                                            Client* client,
                                            Buffer* mini_buffer,
                                            void* data) {
    WITH_BUFFER(buffer, client->selected_buffer_id(), {
        cz::String query = mini_buffer->contents.stringify(cz::heap_allocator());
        CZ_DEFER(query.drop(cz::heap_allocator()));
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            SEARCH_QUERY_THEN(search_forward, {
                buffer->cursors[c] = new_cursor;
                buffer->show_marks = true;
            });
        }
    });
}

void command_search_forward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        if (buffer->show_marks) {
            for (size_t c = 0; c < buffer->cursors.len(); ++c) {
                SEARCH_SLICE_THEN(search_forward_slice, buffer->cursors[c] = new_cursor);
            }
        } else {
            Message message;
            message.tag = Message::RESPOND_TEXT;
            message.text = "Search forward: ";
            message.response_callback = command_search_forward_callback;
            message.response_callback_data = nullptr;
            source.client->show_message(message);
        }
    });
}

static void command_search_backward_callback(Editor* editor,
                                             Client* client,
                                             Buffer* mini_buffer,
                                             void* data) {
    WITH_BUFFER(buffer, client->selected_buffer_id(), {
        cz::String query = mini_buffer->contents.stringify(cz::heap_allocator());
        CZ_DEFER(query.drop(cz::heap_allocator()));
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            SEARCH_QUERY_THEN(search_backward, {
                buffer->cursors[c] = new_cursor;
                buffer->show_marks = true;
            });
        }
    });
}

void command_search_backward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        if (buffer->show_marks) {
            for (size_t c = 0; c < buffer->cursors.len(); ++c) {
                SEARCH_SLICE_THEN(search_backward_slice, buffer->cursors[c] = new_cursor);
            }
        } else {
            Message message;
            message.tag = Message::RESPOND_TEXT;
            message.text = "Search backward: ";
            message.response_callback = command_search_backward_callback;
            message.response_callback_data = nullptr;
            source.client->show_message(message);
        }
    });
}

void command_one_window(Editor* editor, Command_Source source) {
    Buffer_Id buffer_id = source.client->selected_buffer_id();
    Window::drop(source.client->window);
    source.client->window = Window::create(buffer_id);
    source.client->_selected_window = source.client->window;
}

void command_split_window_horizontal(Editor* editor, Command_Source source) {
    Window* top = Window::create(source.client->selected_buffer_id());
    Window* bottom = Window::create(source.client->selected_buffer_id());

    Window* selected_window = source.client->_selected_window;
    selected_window->tag = Window::HORIZONTAL_SPLIT;

    selected_window->v.horizontal_split.top = top;
    top->parent = selected_window;

    selected_window->v.horizontal_split.bottom = bottom;
    bottom->parent = selected_window;

    source.client->_selected_window = top;
}

void command_split_window_vertical(Editor* editor, Command_Source source) {
    Window* left = Window::create(source.client->selected_buffer_id());
    Window* right = Window::create(source.client->selected_buffer_id());

    Window* selected_window = source.client->_selected_window;
    selected_window->tag = Window::VERTICAL_SPLIT;

    selected_window->v.vertical_split.left = left;
    left->parent = selected_window;

    selected_window->v.vertical_split.right = right;
    right->parent = selected_window;

    source.client->_selected_window = left;
}

}
