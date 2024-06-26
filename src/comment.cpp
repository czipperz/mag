#include "comment.hpp"

#include <cz/defer.hpp>
#include <cz/format.hpp>
#include "edit.hpp"
#include "match.hpp"
#include "movement.hpp"

namespace mag {

uint64_t visual_column_for_aligned_line_comments(const Mode& mode,
                                                 Contents_Iterator start,
                                                 uint64_t end) {
    uint64_t start_offset = -1;
    bool set_offset = false;
    for (Contents_Iterator s2 = start; s2.position < end;) {
        // If we're not at an empty line then count the indentation.
        if (s2.get() != '\n') {
            uint64_t column = 0;
            for (;; s2.advance()) {
                char ch = s2.get();
                // Reached end of indentation.
                if (!cz::is_space(ch)) {
                    break;
                }

                uint64_t before = column;
                column = char_visual_columns(mode, ch, column);

                // This line is more indented than the previous line.
                if (column > start_offset) {
                    // If we hit a tab that spans the column then retreat to the tab boundary.
                    if (!mode.comment_break_tabs && before < start_offset) {
                        column = before;
                    }
                    break;
                }
            }

            // If there is no previous line or we are less indented than the previous
            // line then we should indent the entire block at our indentation.
            if (column < start_offset) {
                set_offset = true;
                start_offset = column;
            }
        }

        end_of_line(&s2);
        forward_char(&s2);
    }

    // If we don't set the offset then it is 0 for all lines.
    if (!set_offset) {
        start_offset = 0;
    }

    return start_offset;
}

/// Fix mixed tabs and spaces by breaking all tabs at
/// and after the visual_column point into spaces.
static void go_to_visual_column_and_break_tabs(const Mode& mode,
                                               Contents_Iterator* start,
                                               uint64_t visual_column,
                                               uint64_t* offset,
                                               uint64_t* offset_after,
                                               Transaction* transaction) {
    CZ_DEBUG_ASSERT(at_start_of_line(*start));

    // Find the start of the region to be replaced by spaces.
    auto s2 = *start;
    uint64_t column_start_spaces = 0;
    if (visual_column != 0) {
        for (;; s2.advance()) {
            char ch = s2.get();
            CZ_DEBUG_ASSERT(cz::is_space(ch));

            uint64_t after = char_visual_columns(mode, ch, column_start_spaces);
            if (after < visual_column) {
                column_start_spaces = after;
            } else if (after == visual_column) {
                column_start_spaces = after;
                s2.advance();
                break;
            } else {
                // Multi column character.
                break;
            }
        }
    }
    *start = s2;

    // Find the end of the region to be replaced by spaces.
    bool all_spaces = true;
    uint64_t column_end_spaces = column_start_spaces;
    for (;; s2.advance()) {
        char ch = s2.get();

        if (ch == '\t') {
            all_spaces = false;
        } else if (ch != ' ') {
            break;
        }

        column_end_spaces = char_visual_columns(mode, ch, column_end_spaces);
    }

    // No tabs to break.
    if (all_spaces) {
        return;
    }

    // Remove all indent.
    Edit remove_region;
    remove_region.value =
        start->contents->slice(transaction->value_allocator(), *start, s2.position);
    remove_region.position = start->position + *offset;
    remove_region.flags = Edit::REMOVE;
    transaction->push(remove_region);

    // Put a space for each column.
    cz::String spaces = {};
    spaces.reserve(transaction->value_allocator(), column_end_spaces - column_start_spaces);
    spaces.push_many(' ', column_end_spaces - column_start_spaces);

    // Insert the spaces.
    Edit insert_spaces;
    insert_spaces.value = SSOStr::from_constant(spaces);
    if (insert_spaces.value.is_short()) {
        spaces.drop(transaction->value_allocator());
    }
    insert_spaces.position = start->position + *offset;
    insert_spaces.flags = Edit::INSERT_AFTER_POSITION;
    transaction->push(insert_spaces);

    // We need to track the offset of edits on the next line and the offset
    // to insert the comment (which will be in the middle of the spaces).
    *offset_after += insert_spaces.value.len() - remove_region.value.len();
    *offset += visual_column - column_start_spaces;
}

void insert_line_comments(Transaction* transaction,
                          uint64_t* offset,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          uint64_t visual_column,
                          cz::Str comment_start) {
    cz::String comment_start_space_string =
        cz::format(transaction->value_allocator(), comment_start, ' ');
    SSOStr comment_start_space = SSOStr::from_constant(comment_start_space_string);
    if (comment_start_space.is_short()) {
        comment_start_space_string.drop(transaction->value_allocator());
    }

    while (start.position < end) {
        if (start.get() == '\n') {
            // On empty lines insert indentation and then `comment_start`.
            uint64_t tabs, spaces;
            analyze_indent(mode, visual_column, &tabs, &spaces);

            cz::String string = {};
            string.reserve(transaction->value_allocator(), tabs + spaces + comment_start.len);
            string.push_many('\t', tabs);
            string.push_many(' ', spaces);
            string.append(comment_start);

            Edit edit;
            edit.value = SSOStr::from_constant(string);
            edit.position = start.position + *offset;
            *offset += string.len;
            edit.flags = Edit::INSERT_AFTER_POSITION;
            transaction->push(edit);

            if (edit.value.is_short()) {
                string.drop(transaction->value_allocator());
            }
        } else {
            // Go to the visual column and break tabs after it.
            uint64_t offset_after = *offset;
            if (mode.comment_break_tabs) {
                go_to_visual_column_and_break_tabs(mode, &start, visual_column, offset,
                                                   &offset_after, transaction);
            } else {
                go_to_visual_column(mode, &start, visual_column);
            }

            // On non-empty lines insert `comment_start` then a space at the aligned column.
            Edit edit;
            edit.value = comment_start_space;
            edit.position = start.position + *offset;
            edit.flags = Edit::INSERT_AFTER_POSITION;
            transaction->push(edit);

            *offset = offset_after + edit.value.len();
        }

        end_of_line(&start);
        forward_char(&start);
    }
}

void insert_line_comments(Transaction* transaction,
                          uint64_t* offset,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          cz::Str comment_start) {
    start_of_line(&start);
    uint64_t visual_column = visual_column_for_aligned_line_comments(mode, start, end);
    insert_line_comments(transaction, offset, mode, start, end, visual_column, comment_start);
}

void remove_line_comments(Transaction* transaction,
                          uint64_t* offset,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          cz::Str comment_start) {
    start_of_line(&start);

    while (start.position < end) {
        Contents_Iterator sol = start;
        forward_through_whitespace(&start);

        if (!looking_at(start, comment_start)) {
        next_line:
            end_of_line(&start);
            forward_char(&start);
            continue;
        }

        Contents_Iterator after = start;
        after.advance(comment_start.len);

        if (after.get() == ' ') {
            after.advance();
        }

        if (after.at_eob() || after.get() == '\n') {
            // Delete all indent and the comment.
            Edit remove_line;
            remove_line.value =
                sol.contents->slice(transaction->value_allocator(), sol, after.position);
            remove_line.position = sol.position + *offset;
            remove_line.flags = Edit::REMOVE;
            transaction->push(remove_line);
            *offset -= remove_line.value.len();
            goto next_line;
        }

        // Remove the comment.
        Edit remove_comment;
        remove_comment.value =
            start.contents->slice(transaction->value_allocator(), start, after.position);
        remove_comment.position = start.position + *offset;
        remove_comment.flags = Edit::REMOVE;
        transaction->push(remove_comment);
        // Note: offset is edited after we fix the indent because

        //
        // Fix indentation.
        // TODO: merge with change_indent() ???
        //

        // First count the columns before the comment.  Then count the columns after it.
        uint64_t column = 0;
        uint64_t before_tabs = 0, before_spaces = 0;
        Contents_Iterator it = sol;
        for (; it.position < start.position; it.advance()) {
            char ch = it.get();
            if (ch == ' ') {
                before_spaces++;
            } else if (ch == '\t') {
                before_tabs++;
            }
            column = char_visual_columns(mode, ch, column);
        }
        it.advance_to(after.position);
        for (;; it.advance()) {
            char ch = it.get();
            if (!cz::is_blank(ch)) {
                break;
            }
            if (ch == ' ') {
                before_spaces++;
            } else if (ch == '\t') {
                before_tabs++;
            }

            column = char_visual_columns(mode, ch, column);
        }

        // If the desired indent is different then completely replace it.
        uint64_t after_tabs, after_spaces;
        analyze_indent(mode, column, &after_tabs, &after_spaces);
        if (before_tabs != after_tabs || before_spaces != after_spaces) {
            cz::String old_indent = {};
            old_indent.reserve(transaction->value_allocator(), before_tabs + before_spaces);
            sol.contents->slice_into(sol, start.position, &old_indent);
            sol.contents->slice_into(after, it.position, &old_indent);

            cz::String new_indent = {};
            new_indent.reserve(transaction->value_allocator(), after_tabs + after_spaces);
            new_indent.push_many('\t', after_tabs);
            new_indent.push_many(' ', after_spaces);

            Edit remove_old_indent;
            remove_old_indent.value = SSOStr::from_constant(old_indent);
            remove_old_indent.position = sol.position + *offset;
            remove_old_indent.flags = Edit::REMOVE;
            transaction->push(remove_old_indent);

            Edit insert_new_indent;
            insert_new_indent.value = SSOStr::from_constant(new_indent);
            insert_new_indent.position = sol.position + *offset;
            insert_new_indent.flags = Edit::INSERT_AFTER_POSITION;
            transaction->push(insert_new_indent);

            *offset += new_indent.len - old_indent.len;

            if (insert_new_indent.value.is_short()) {
                new_indent.drop(transaction->value_allocator());
            }
            if (remove_old_indent.value.is_short()) {
                old_indent.drop(transaction->value_allocator());
            }
        }

        *offset -= remove_comment.value.len();

        goto next_line;
    }
}

void generic_line_comment(Client* client,
                          Buffer* buffer,
                          Window_Unified* window,
                          cz::Str comment_start,
                          bool add) {
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    Contents_Iterator start = buffer->contents.start();
    if (window->show_marks) {
        for (size_t i = 0; i < cursors.len; ++i) {
            start.advance_to(cursors[i].start());
            if (add) {
                insert_line_comments(&transaction, &offset, buffer->mode, start, cursors[i].end(),
                                     comment_start);
            } else {
                remove_line_comments(&transaction, &offset, buffer->mode, start, cursors[i].end(),
                                     comment_start);
            }
        }
    } else {
        for (size_t i = 0; i < cursors.len; ++i) {
            start.advance_to(cursors[i].point);
            Contents_Iterator end = start;
            forward_char(&end);
            if (add) {
                insert_line_comments(&transaction, &offset, buffer->mode, start, end.position,
                                     comment_start);
            } else {
                remove_line_comments(&transaction, &offset, buffer->mode, start, end.position,
                                     comment_start);
            }
        }

        // If there is only one cursor and no region selected then move to the next line.
        if (cursors.len == 1) {
            Contents_Iterator it = buffer->contents.iterator_at(cursors[0].point);
            forward_line(buffer->mode, &it);
            cursors[0].point = it.position;
        }
    }

    transaction.commit(client);
}

}
