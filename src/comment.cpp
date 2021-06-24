#include "comment.hpp"

#include "edit.hpp"
#include "movement.hpp"

namespace mag {

uint64_t visual_column_for_aligned_line_comments(const Mode& mode,
                                                 Contents_Iterator start,
                                                 uint64_t end) {
    uint64_t start_offset = 0;
    bool set_offset = false;
    bool any_differences = false;
    for (Contents_Iterator s2 = start; s2.position < end;) {
        // If we're not at an empty line then count the indentation.
        if (s2.get() != '\n') {
            uint64_t column = 0;
            for (;; s2.advance()) {
                char ch = s2.get();
                // Reached end of indentation.
                if (!cz::is_space(ch)) {
                    if (set_offset && column != start_offset) {
                        any_differences = true;
                    }
                    break;
                }

                uint64_t after = char_visual_columns(mode, ch, column);
                // This line is more indented than the previous line; this
                // may happen if we hit a tab.  If so then we may want to
                // use the column before the tab as the start_offset.
                if (set_offset && after > start_offset) {
                    any_differences = true;
                    break;
                }
                column = after;
            }

            // If there is no previous line or we are less indented than the previous
            // line then we should indent the entire block at our indentation.
            if (!set_offset || column < start_offset) {
                set_offset = true;
                start_offset = column;
            }
        }

        end_of_line(&s2);
        forward_char(&s2);
    }

    // Align backward to the tab boundary if tabs are used.
    if (any_differences && set_offset && mode.use_tabs) {
        start_offset -= (start_offset % mode.tab_width);
    }

    // If we don't set the offset then it is 0 for all lines.
    if (!set_offset) {
        start_offset = 0;
    }

    return start_offset;
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
            go_to_visual_column(mode, &start, visual_column);

            Edit edit;
            edit.value = SSOStr::from_constant(yes_space);
            edit.position = start.position + offset;
            offset += edit.value.len();
            edit.flags = Edit::INSERT_AFTER_POSITION;
            transaction->push(edit);
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
    uint64_t visual_column = visual_column_for_aligned_line_comments(mode, start, end);
    insert_line_comments(transaction, mode, start, end, visual_column, no_space, yes_space);
}

}
