#include "commands.hpp"

#include <cz/defer.hpp>
#include <cz/option.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "transaction.hpp"

namespace mag {

void command_set_mark(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            buffer->cursors[c].mark = buffer->cursors[c].point;
        }
        buffer->show_marks = true;
    });
}

void command_swap_mark_point(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            cz::swap(buffer->cursors[c].point, buffer->cursors[c].mark);
        }
    });
}

static void save_copy(Buffer* buffer, size_t c, SSOStr value) {
    Copy_Chain* chain = buffer->copy_buffer.allocator().alloc<Copy_Chain>();
    chain->value = value;
    chain->previous = buffer->cursors[c].copy_chain;
    buffer->cursors[c].copy_chain = chain;
}

static size_t sum_region_sizes(Buffer* buffer) {
    uint64_t sum = 0;
    for (size_t c = 0; c < buffer->cursors.len(); ++c) {
        uint64_t start = buffer->cursors[c].start();
        uint64_t end = buffer->cursors[c].end();
        sum += end - start;
    }
    return (size_t)sum;
}

void command_cut(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION({
        transaction.init(buffer->cursors.len(), sum_region_sizes(buffer));

        size_t offset = 0;
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            uint64_t start = buffer->cursors[c].start();
            uint64_t end = buffer->cursors[c].end();

            Edit edit;
            edit.value = buffer->contents.slice(transaction.value_allocator(), start, end);
            edit.position = start - offset;
            offset += end - start;
            edit.is_insert = false;
            transaction.push(edit);

            save_copy(buffer, c, edit.value);
        }

        buffer->show_marks = false;
    }));
}

void command_copy(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            uint64_t start = buffer->cursors[c].start();
            uint64_t end = buffer->cursors[c].end();
            // :CopyLeak We allocate here.
            save_copy(buffer, c,
                      buffer->contents.slice(buffer->copy_buffer.allocator(), start, end));
        }

        buffer->show_marks = false;
    });
}

void command_paste(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION({
        // :CopyLeak Probably we will need to copy all the values herea.
        transaction.init(buffer->cursors.len(), 0);

        size_t offset = 0;
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            if (buffer->cursors[c].copy_chain) {
                Edit edit;
                // :CopyLeak We sometimes use the value here, but we could also
                // just copy a bunch of stuff then close the cursors and leak
                // that memory.
                edit.value = buffer->cursors[c].copy_chain->value;
                edit.position = buffer->cursors[c].point + offset;
                offset += edit.value.len();
                edit.is_insert = true;
                transaction.push(edit);
            }
        }
    }));
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

void command_end_of_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        uint64_t len = buffer->contents.len();
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            buffer->cursors[c].point = len;
        }
    });
}

void command_start_of_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            buffer->cursors[c].point = 0;
        }
    });
}

void command_end_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(end_of_line));
}

void command_start_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(start_of_line));
}

void command_start_of_line_text(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(TRANSFORM_POINTS(start_of_line_text));
}

void command_shift_line_forward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        cz::Vector<uint64_t> cursor_positions = {};
        CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
        cursor_positions.reserve(cz::heap_allocator(), buffer->cursors.len());

        WITH_TRANSACTION({
            uint64_t sum_line_lengths = 0;
            for (size_t c = 0; c < buffer->cursors.len(); ++c) {
                uint64_t start = start_of_line(buffer, buffer->cursors[c].point);
                uint64_t end = end_of_line(buffer, buffer->cursors[c].point);
                sum_line_lengths += end - start + 1;
            }

            transaction.init(buffer->cursors.len() * 3, (size_t)sum_line_lengths);

            for (size_t c = 0; c < buffer->cursors.len(); ++c) {
                uint64_t start = start_of_line(buffer, buffer->cursors[c].point);
                uint64_t end = end_of_line(buffer, buffer->cursors[c].point);
                uint64_t column = buffer->cursors[c].point - start;
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
                        buffer->contents.slice(transaction.value_allocator(), start, end);

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

                SSOStr value = buffer->contents.slice(transaction.value_allocator(), start, end);

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

        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            buffer->cursors[c].point = cursor_positions[c];
        }
    });
}

void command_shift_line_backward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        cz::Vector<uint64_t> cursor_positions = {};
        CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
        cursor_positions.reserve(cz::heap_allocator(), buffer->cursors.len());

        WITH_TRANSACTION({
            uint64_t sum_line_lengths = 0;
            for (size_t c = 0; c < buffer->cursors.len(); ++c) {
                uint64_t start = start_of_line(buffer, buffer->cursors[c].point);
                uint64_t end = end_of_line(buffer, buffer->cursors[c].point);
                sum_line_lengths += end - start + 1;
            }

            transaction.init(buffer->cursors.len() * 3, (size_t)sum_line_lengths);

            for (size_t c = 0; c < buffer->cursors.len(); ++c) {
                uint64_t start = start_of_line(buffer, buffer->cursors[c].point);
                uint64_t end = end_of_line(buffer, buffer->cursors[c].point);
                uint64_t column = buffer->cursors[c].point - start;
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
                        buffer->contents.slice(transaction.value_allocator(), start - 1, end);

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

                SSOStr value = buffer->contents.slice(transaction.value_allocator(), start, end);

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

        for (size_t c = 0; c < buffer->cursors.len(); ++c) {
            buffer->cursors[c].point = cursor_positions[c];
        }
    });
}

void command_delete_backward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        if (source.previous_command == command_delete_backward_char) {
            CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
            Commit commit = buffer->commits[buffer->commit_index - 1];
            size_t len = commit.edits[0].value.len();
            if (len < SSOStr::MAX_SHORT_LEN) {
                CZ_DEBUG_ASSERT(commit.edits.len == buffer->cursors.len());
                buffer->undo();

                WITH_TRANSACTION({
                    transaction.init(commit.edits.len, 0);

                    uint64_t offset = 1;
                    for (size_t e = 0; e < commit.edits.len; ++e) {
                        if (buffer->cursors[e].point == 0) {
                            continue;
                        }

                        CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                        CZ_DEBUG_ASSERT(commit.edits[e].value.len() == len);

                        Edit edit;
                        memcpy(edit.value.short_._buffer + 1, commit.edits[e].value.short_._buffer,
                               len);
                        edit.value.short_._buffer[0] =
                            buffer->contents[buffer->cursors[e].point - len - 1];
                        edit.value.short_.set_len(len + 1);
                        edit.position = commit.edits[e].position - offset;
                        offset++;
                        edit.is_insert = false;
                        transaction.push(edit);
                    }
                });
                return;
            }
        }

        WITH_TRANSACTION(DELETE_BACKWARD(backward_char));
    });
}

void command_delete_forward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        if (source.previous_command == command_delete_forward_char) {
            CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
            Commit commit = buffer->commits[buffer->commit_index - 1];
            size_t len = commit.edits[0].value.len();
            if (len < SSOStr::MAX_SHORT_LEN) {
                CZ_DEBUG_ASSERT(commit.edits.len == buffer->cursors.len());
                buffer->undo();

                WITH_TRANSACTION({
                    transaction.init(commit.edits.len, 0);
                    for (size_t e = 0; e < commit.edits.len; ++e) {
                        CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                        CZ_DEBUG_ASSERT(commit.edits[e].value.len() == len);

                        Edit edit;
                        memcpy(edit.value.short_._buffer, commit.edits[e].value.short_._buffer,
                               len);
                        edit.value.short_._buffer[len] = buffer->contents[buffer->cursors[e].point];
                        edit.value.short_.set_len(len + 1);
                        edit.position = commit.edits[e].position - e;
                        edit.is_insert = false;
                        transaction.push(edit);
                    }
                });
                return;
            }
        }

        WITH_TRANSACTION(DELETE_FORWARD(forward_char));
    });
}

void command_delete_backward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION(DELETE_BACKWARD(backward_word)));
}

void command_delete_forward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION(DELETE_FORWARD(forward_word)));
}

void command_open_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        insert_char(buffer, '\n');
        TRANSFORM_POINTS(backward_char);
    });
}

void command_insert_newline(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(insert_char(buffer, '\n'));
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
        source.client->hide_mini_buffer(editor);
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
        cursor.copy_chain = buffer->cursors.last().copy_chain;

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
        cursor.copy_chain = buffer->cursors[0].copy_chain;

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
            new_cursor.copy_chain = buffer->cursors[c].copy_chain;    \
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
            new_cursor.copy_chain = buffer->cursors[c].copy_chain;   \
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

static bool is_first(Window* parent, Window* child) {
    switch (parent->tag) {
    case Window::UNIFIED:
        CZ_PANIC("Unified window has children");

    case Window::VERTICAL_SPLIT:
        return parent->v.vertical_split.left == child;

    case Window::HORIZONTAL_SPLIT:
        return parent->v.horizontal_split.top == child;
    }

    CZ_PANIC("");
}

static Window* first(Window* window) {
    switch (window->tag) {
    case Window::UNIFIED:
        return window;

    case Window::VERTICAL_SPLIT:
        return first(window->v.vertical_split.left);

    case Window::HORIZONTAL_SPLIT:
        return first(window->v.horizontal_split.top);
    }

    CZ_PANIC("");
}

static Window* second_side(Window* window) {
    switch (window->tag) {
    case Window::UNIFIED:
        CZ_PANIC("Unified window has children");

    case Window::VERTICAL_SPLIT:
        return window->v.vertical_split.right;

    case Window::HORIZONTAL_SPLIT:
        return window->v.horizontal_split.bottom;
    }

    CZ_PANIC("");
}

void command_cycle_window(Editor* editor, Command_Source source) {
    Window* child;
    Window* parent = source.client->_selected_window;
    do {
        child = parent;
        parent = child->parent;
        if (!parent) {
            source.client->_selected_window = first(source.client->window);
            return;
        }
    } while (!is_first(parent, child));

    source.client->_selected_window = first(second_side(parent));
}

static void command_open_file_callback(Editor* editor,
                                       Client* client,
                                       Buffer* mini_buffer,
                                       void* data) {
    cz::String query = mini_buffer->contents.stringify(cz::heap_allocator());
    CZ_DEFER(query.drop(cz::heap_allocator()));
    open_file(editor, client, query);
}

void command_open_file(Editor* editor, Command_Source source) {
    Message message;
    message.tag = Message::RESPOND_FILE;
    message.text = "Open file: ";
    message.response_callback = command_open_file_callback;
    message.response_callback_data = nullptr;

    cz::String default_value = {};
    CZ_DEFER(default_value.drop(cz::heap_allocator()));
    bool has_default_value;
    WITH_SELECTED_BUFFER({
        has_default_value = buffer->path.find('/') != nullptr;
        if (has_default_value) {
            default_value = buffer->path.clone(cz::heap_allocator());
        }
    });

    if (has_default_value) {
        WITH_BUFFER(buffer, source.client->mini_buffer_id(), WITH_TRANSACTION({
                        transaction.init(1, default_value.len());
                        Edit edit;
                        edit.value.init_duplicate(transaction.value_allocator(), default_value);
                        edit.position = 0;
                        edit.is_insert = true;
                        transaction.push(edit);
                    }));
    }

    source.client->show_message(message);
}

void command_save_file(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        if (!buffer->path.find('/')) {
            Message message = {};
            message.tag = Message::SHOW;
            message.text = "File must have path";
            source.client->show_message(message);
            return;
        }

        if (!save_contents(&buffer->contents, buffer->path.buffer())) {
            Message message = {};
            message.tag = Message::SHOW;
            message.text = "Error saving file";
            source.client->show_message(message);
            return;
        }

        buffer->mark_saved();
    });
}

}
