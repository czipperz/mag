#include "comment.hpp"

#include "edit.hpp"
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

                column = char_visual_columns(mode, ch, column);
                // This line is more indented than the previous line.
                if (column > start_offset) {
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

void go_to_visual_column_and_break_tabs(const Mode& mode,
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

    Edit remove_region;
    remove_region.value =
        start->contents->slice(transaction->value_allocator(), *start, s2.position);
    remove_region.position = start->position + *offset;
    remove_region.flags = Edit::REMOVE;
    transaction->push(remove_region);

    cz::String spaces = {};
    spaces.reserve(transaction->value_allocator(), column_end_spaces - column_start_spaces);
    spaces.push_many(' ', column_end_spaces - column_start_spaces);

    Edit insert_spaces;
    insert_spaces.value = SSOStr::from_constant(spaces);
    if (insert_spaces.value.is_short()) {
        spaces.drop(transaction->value_allocator());
    }
    insert_spaces.position = start->position + *offset;
    insert_spaces.flags = Edit::INSERT_AFTER_POSITION;
    transaction->push(insert_spaces);

    *offset_after += insert_spaces.value.len() - remove_region.value.len();
    *offset += visual_column - column_start_spaces;
}

void insert_line_comments(Transaction* transaction,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          uint64_t visual_column,
                          cz::Str no_space,
                          cz::Str yes_space) {
    uint64_t offset = 0;

    while (start.position < end) {
        if (start.get() == '\n') {
            // On empty lines insert indentation and then `no_space`.
            uint64_t tabs, spaces;
            analyze_indent(mode, visual_column, &tabs, &spaces);

            cz::String string = {};
            string.reserve(transaction->value_allocator(), tabs + spaces + no_space.len);
            string.push_many('\t', tabs);
            string.push_many(' ', spaces);
            string.append(no_space);

            Edit edit;
            edit.value = SSOStr::from_constant(string);
            edit.position = start.position + offset;
            offset += string.len();
            edit.flags = Edit::INSERT_AFTER_POSITION;
            transaction->push(edit);

            if (edit.value.is_short()) {
                string.drop(transaction->value_allocator());
            }
        } else {
            // On non-empty lines insert `yes_space` at the aligned column.

            // Fix mixed tabs and spaces by breaking all tabs at
            // and after the visual_column point into spaces.
            uint64_t offset_after = offset;
            go_to_visual_column_and_break_tabs(mode, &start, visual_column, &offset, &offset_after,
                                               transaction);

            Edit edit;
            edit.value = SSOStr::from_constant(yes_space);
            edit.position = start.position + offset;
            edit.flags = Edit::INSERT_AFTER_POSITION;
            transaction->push(edit);

            offset = offset_after + edit.value.len();
        }

        end_of_line(&start);
        forward_char(&start);
    }
}

void insert_line_comments(Transaction* transaction,
                          const Mode& mode,
                          Contents_Iterator start,
                          uint64_t end,
                          cz::Str no_space,
                          cz::Str yes_space) {
    start_of_line(&start);
    uint64_t visual_column = visual_column_for_aligned_line_comments(mode, start, end);
    insert_line_comments(transaction, mode, start, end, visual_column, no_space, yes_space);
}

}
