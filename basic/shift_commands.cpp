#include "shift_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"

namespace mag {
namespace basic {

void command_shift_line_forward(Editor* editor, Command_Source source) {
    // Logically shift a line forwards.  This works by physically shifting the opposite line
    // forward.

    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t sum_line_lengths = 0;
    size_t num_edits = cursors.len * 2;
    for (size_t c = 0; c < cursors.len; ++c) {
        // `start_next` is the start of the line we are going to delete (next line).
        Contents_Iterator start_next;
        if (window->show_marks) {
            start_next = buffer->contents.iterator_at(cursors[c].end());

            // Special case: when selecting a region that ends at the start of a line, pretend
            // we instead ended at the end of the previous line.  This allows the user to go to
            // the beginning of a line, set the mark, walk down a few lines, and then shift the
            // region.
            if (!start_next.at_eob() && cursors[c].point != cursors[c].mark) {
                Contents_Iterator it = start_next;
                it.retreat();
                if (it.get() == '\n') {
                    start_next = it;
                    if (cursors[c].point > cursors[c].mark) {
                        --cursors[c].point;
                    } else {
                        --cursors[c].mark;
                    }
                }
            }
        } else {
            start_next = buffer->contents.iterator_at(cursors[c].point);
        }

        end_of_line(&start_next);
        forward_char(&start_next);
        if (start_next.at_eob()) {
            num_edits -= 2;
            continue;
        }

        // `end_next` is the end of the line we are going to delete (next line).
        Contents_Iterator end_next = start_next;
        end_of_line(&end_next);

        // Check to see if we can merge with the next edit (at the next cursor).
        if (c + 1 < cursors.len) {
            uint64_t next_cursor_position;
            if (window->show_marks) {
                next_cursor_position = cursors[c + 1].start();
            } else {
                next_cursor_position = cursors[c + 1].point;
            }

            if (next_cursor_position <= end_next.position) {
                // The edits overlap so we do merge.
                num_edits -= 2;
                continue;
            }
        }

        sum_line_lengths += end_next.position - start_next.position;
    }

    Transaction transaction;
    transaction.init(num_edits, (size_t)sum_line_lengths + num_edits);
    CZ_DEFER(transaction.drop());

    bool override_start = false;
    Contents_Iterator start;
    for (size_t c = 0; c < cursors.len; ++c) {
        // `start_next` is the start of the line we are going to delete (next line).
        Contents_Iterator start_next;
        if (window->show_marks) {
            start_next = buffer->contents.iterator_at(cursors[c].end());

            // Special case: when selecting a region that ends at the start of a line, pretend
            // we instead ended at the end of the previous line.  This allows the user to go to
            // the beginning of a line, set the mark, walk down a few lines, and then shift the
            // region.
            if (!start_next.at_eob() && cursors[c].point != cursors[c].mark) {
                Contents_Iterator it = start_next;
                it.retreat();
                if (it.get() == '\n') {
                    start_next = it;
                    if (cursors[c].point > cursors[c].mark) {
                        --cursors[c].point;
                    } else {
                        --cursors[c].mark;
                    }
                }
            }
        } else {
            start_next = buffer->contents.iterator_at(cursors[c].point);
        }

        if (override_start) {
            // We are merging with the previous edit, which works on a region directly before us in
            // the file.  `start` is now the start of the total merged region.
            override_start = false;
        } else {
            if (window->show_marks) {
                start = buffer->contents.iterator_at(cursors[c].start());
            } else {
                start = start_next;
            }
        }

        end_of_line(&start_next);
        forward_char(&start_next);
        if (start_next.at_eob()) {
            continue;
        }

        start_of_line(&start);

        // `end_next` is the end of the line we are going to delete (next line).
        Contents_Iterator end_next = start_next;
        end_of_line(&end_next);

        // Check to see if we can merge with the next edit (at the next cursor).
        if (c + 1 < cursors.len) {
            uint64_t next_cursor_position;
            if (window->show_marks) {
                next_cursor_position = cursors[c + 1].start();
            } else {
                next_cursor_position = cursors[c + 1].point;
            }

            if (next_cursor_position <= end_next.position) {
                // The edits overlap so we do merge.
                override_start = true;
                continue;
            }
        }

        //     def\nghi
        //     [    [  )
        // start    ^  ^
        // start_next  end_next
        char* buf = (char*)transaction.value_allocator().alloc(
            {end_next.position - start_next.position + 2, 1});
        buf[0] = '\n';
        buf[end_next.position - start_next.position + 1] = '\n';
        buffer->contents.slice_into(start_next, end_next.position, buf + 1);

        Edit remove;
        remove.value.init_from_constant({buf, end_next.position - start_next.position + 1});
        remove.position = start_next.position - 1;
        remove.flags = Edit::REMOVE_AFTER_POSITION;
        transaction.push(remove);

        Edit insert;
        insert.value.init_from_constant({buf + 1, end_next.position - start_next.position + 1});
        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(buffer);
}

void command_shift_line_backward(Editor* editor, Command_Source source) {
    // Logically shift a line backwards.  This works by physically shifting the opposite line
    // forward.

    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    uint64_t sum_line_lengths = 0;
    size_t num_edits = cursors.len * 2;
    for (size_t c = cursors.len; c-- > 0;) {
        // `end_prev` is the end of the line we are going to delete (previous line).
        Contents_Iterator end_prev;
        if (window->show_marks) {
            end_prev = buffer->contents.iterator_at(cursors[c].start());
        } else {
            end_prev = buffer->contents.iterator_at(cursors[c].point);
        }

        start_of_line(&end_prev);
        backward_char(&end_prev);
        if (end_prev.at_bob()) {
            num_edits -= 2;
            continue;
        }

        // `start_prev` is the start of the line we are going to delete (previous line).
        Contents_Iterator start_prev = end_prev;
        start_of_line(&start_prev);

        // Check to see if we can merge with the next edit (at the previous cursor).
        if (c >= 1) {
            uint64_t next_cursor_position;
            if (window->show_marks) {
                next_cursor_position = cursors[c - 1].end();
            } else {
                next_cursor_position = cursors[c - 1].point;
            }

            if (next_cursor_position >= start_prev.position) {
                // The edits overlap so we do merge.
                num_edits -= 2;
                continue;
            }
        }

        // Add the length of the line to be deleted (previous line).
        sum_line_lengths += end_prev.position - start_prev.position;
    }

    Transaction transaction;
    transaction.init(num_edits, (size_t)sum_line_lengths + num_edits);
    CZ_DEFER(transaction.drop());

    bool override_end = false;
    Contents_Iterator end;
    for (size_t c = cursors.len; c-- > 0;) {
        // `end_prev` is the end of the line we are going to delete (previous line).
        Contents_Iterator end_prev;
        if (window->show_marks) {
            end_prev = buffer->contents.iterator_at(cursors[c].start());
        } else {
            end_prev = buffer->contents.iterator_at(cursors[c].point);
        }

        if (override_end) {
            // We are merging with the previous edit, which works on a region directly after us in
            // the file.  `end` is now the end of the total merged region.
            override_end = false;
        } else {
            if (window->show_marks) {
                end = buffer->contents.iterator_at(cursors[c].end());

                // Special case: when selecting a region that ends at the start of a line, pretend
                // we instead ended at the end of the previous line.  This allows the user to go to
                // the beginning of a line, set the mark, walk down a few lines, and then shift the
                // region.
                if (!end.at_bob() && end.position > end_prev.position) {
                    Contents_Iterator it = end;
                    it.retreat();
                    if (it.get() == '\n') {
                        end = it;
                        // In order for the cursor to move with the shift, we have to include it in
                        // the region that is edited.
                        if (cursors[c].point > cursors[c].mark) {
                            --cursors[c].point;
                        } else {
                            --cursors[c].mark;
                        }
                    }
                }
            } else {
                end = end_prev;
            }
        }

        start_of_line(&end_prev);
        backward_char(&end_prev);
        if (end_prev.at_bob()) {
            num_edits -= 2;
            continue;
        }

        end_of_line(&end);

        Contents_Iterator start_prev = end_prev;
        start_of_line(&start_prev);

        // Check to see if we can merge with the next edit (at the previous cursor).
        if (c >= 1) {
            uint64_t next_cursor_position;
            if (window->show_marks) {
                next_cursor_position = cursors[c - 1].start();
            } else {
                next_cursor_position = cursors[c - 1].point;
            }

            if (next_cursor_position >= start_prev.position) {
                // The edits overlap so we do merge.
                override_end = true;
                continue;
            }
        }

        //          def\nghi
        //          [  )    )
        //          ^  ^    end
        // start_prev  end_prev
        char* buf = (char*)transaction.value_allocator().alloc(
            {end_prev.position - start_prev.position + 2, 1});
        buf[0] = '\n';
        buf[end_prev.position - start_prev.position + 1] = '\n';
        buffer->contents.slice_into(start_prev, end_prev.position, buf + 1);

        Edit insert;
        insert.value.init_from_constant({buf, end_prev.position - start_prev.position + 1});
        insert.position = end.position;
        insert.flags = Edit::INSERT_AFTER_POSITION;
        transaction.push(insert);

        Edit remove;
        remove.value.init_from_constant({buf + 1, end_prev.position - start_prev.position + 1});
        remove.position = start_prev.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);
    }

    transaction.commit(buffer);
}

}
}
