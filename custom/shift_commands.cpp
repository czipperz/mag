#include "shift_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"

namespace mag {
namespace custom {

void command_shift_line_forward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        cz::Slice<Cursor> cursors = window->cursors;

        cz::Vector<uint64_t> cursor_positions = {};
        CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
        cursor_positions.reserve(cz::heap_allocator(), cursors.len);

        WITH_TRANSACTION({
            uint64_t sum_line_lengths = 0;
            for (size_t c = 0; c < cursors.len; ++c) {
                Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
                Contents_Iterator end = start;
                start_of_line(&start);
                end_of_line(&end);
                sum_line_lengths += end.position - start.position + 1;
            }

            transaction.init(cursors.len * 3, (size_t)sum_line_lengths);

            for (size_t c = 0; c < cursors.len; ++c) {
                Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
                Contents_Iterator end = start;
                start_of_line(&start);
                end_of_line(&end);
                uint64_t column = cursors[c].point - start.position;

                Contents_Iterator insertion_point = end;
                forward_char(&insertion_point);
                end_of_line(&insertion_point);
                if (insertion_point.position == end.position) {
                    continue;
                }

                // abc\ndef\nghi\njkl\n
                //      [  )    ^
                //  start  end  ^
                //       insertion_point

                cursor_positions.push(insertion_point.position - (end.position - start.position) +
                                      column);

                if (!insertion_point.at_eob()) {
                    // abc\ndef\nghi\njkl\n
                    //      [    )    ^
                    //  start    end  ^
                    //         insertion_point
                    insertion_point.advance();
                    end.advance();
                } else if (!start.at_bob()) {
                    // abc\ndef\nghi
                    //    [    )    ^
                    //  start end   ^
                    //       insertion_point
                    start.retreat();
                } else {
                    //    abc\ndef
                    //    [    )  ^
                    // start  end ^
                    //     insertion_point
                    end.advance();

                    SSOStr value =
                        buffer->contents.slice(transaction.value_allocator(), start, end.position);

                    Edit edit_insert;
                    cz::Str value_str = value.as_str();
                    edit_insert.value.init_from_constant({value_str.buffer, value_str.len - 1});
                    edit_insert.position = insertion_point.position;
                    edit_insert.is_insert = true;
                    transaction.push(edit_insert);

                    Edit edit_insert_newline;
                    edit_insert_newline.value.init_char('\n');
                    edit_insert_newline.position = insertion_point.position;
                    edit_insert_newline.is_insert = true;
                    transaction.push(edit_insert_newline);

                    Edit edit_delete;
                    edit_delete.value = value;
                    edit_delete.position = start.position;
                    edit_delete.is_insert = false;
                    transaction.push(edit_delete);
                    continue;
                }

                SSOStr value =
                    buffer->contents.slice(transaction.value_allocator(), start, end.position);

                Edit edit_insert;
                edit_insert.value = value;
                edit_insert.position = insertion_point.position;
                edit_insert.is_insert = true;
                transaction.push(edit_insert);

                Edit edit_delete;
                edit_delete.value = value;
                edit_delete.position = start.position;
                edit_delete.is_insert = false;
                transaction.push(edit_delete);
            }
        });

        window->update_cursors(buffer->changes);

        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].point = cursor_positions[c];
        }
    });
}

void command_shift_line_backward(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        cz::Slice<Cursor> cursors = window->cursors;

        cz::Vector<uint64_t> cursor_positions = {};
        CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
        cursor_positions.reserve(cz::heap_allocator(), cursors.len);

        WITH_TRANSACTION({
            uint64_t sum_line_lengths = 0;
            for (size_t c = 0; c < cursors.len; ++c) {
                Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
                Contents_Iterator end = start;
                start_of_line(&start);
                end_of_line(&end);
                sum_line_lengths += end.position - start.position + 1;
            }

            transaction.init(cursors.len * 3, (size_t)sum_line_lengths);

            for (size_t c = 0; c < cursors.len; ++c) {
                Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
                Contents_Iterator end = start;
                start_of_line(&start);
                end_of_line(&end);
                uint64_t column = cursors[c].point - start.position;

                Contents_Iterator insertion_point = start;
                backward_line(&insertion_point);
                if (insertion_point.position == start.position) {
                    continue;
                }

                // abc\ndef\nghi\njkl\n
                //      ^    [  )
                //      ^ start end
                // insertion_point

                cursor_positions.push(insertion_point.position + column);

                if (!end.at_eob()) {
                    // Normal case: pick up newline after end.
                    //
                    //   abc\ndef\nghi\njkl\n
                    //        ^    [    )
                    //        ^  start end
                    // insertion_point
                    end.advance();
                } else if (!insertion_point.at_bob()) {
                    // We don't have the line after us.
                    //
                    // abc\ndef\nghi
                    //    ^    [    )
                    //    ^ start   end
                    // insertion_point
                    start.retreat();
                    insertion_point.retreat();
                } else {
                    // We don't have a line after us or before us.
                    //
                    // abc\ndef
                    //      [  )
                    start.retreat();

                    SSOStr value =
                        buffer->contents.slice(transaction.value_allocator(), start, end.position);

                    Edit edit_delete;
                    edit_delete.value = value;
                    edit_delete.position = start.position;
                    edit_delete.is_insert = false;
                    transaction.push(edit_delete);

                    Edit edit_insert;
                    cz::Str value_str = value.as_str();
                    edit_insert.value.init_from_constant({value_str.buffer + 1, value_str.len - 1});
                    edit_insert.position = insertion_point.position;
                    edit_insert.is_insert = true;
                    transaction.push(edit_insert);

                    Edit edit_insert_newline;
                    edit_insert_newline.value.init_char('\n');
                    edit_insert_newline.position = value_str.len - 1;
                    edit_insert_newline.is_insert = true;
                    transaction.push(edit_insert_newline);
                    continue;
                }

                SSOStr value =
                    buffer->contents.slice(transaction.value_allocator(), start, end.position);

                Edit edit_delete;
                edit_delete.value = value;
                edit_delete.position = start.position;
                edit_delete.is_insert = false;
                transaction.push(edit_delete);

                Edit edit_insert;
                edit_insert.value = value;
                edit_insert.position = insertion_point.position;
                edit_insert.is_insert = true;
                transaction.push(edit_insert);
            }
        });

        window->update_cursors(buffer->changes);

        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].point = cursor_positions[c];
        }
    });
}

}
}
