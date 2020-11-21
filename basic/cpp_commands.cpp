#include "cpp_commands.hpp"

#include <ctype.h>
#include "command_macros.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace cpp {

static bool is_block(Contents_Iterator start, Contents_Iterator end) {
    bool block = false;

    Contents_Iterator sol = start;
    start_of_line_text(&sol);
    if (sol.position < start.position) {
        block = true;
    }

    Contents_Iterator sole = end;
    start_of_line_text(&sole);
    if (sole.position < end.position) {
        block = true;
    }

    return block;
}

void command_comment(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    if (window->show_marks) {
        size_t edits = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
            Contents_Iterator end = start;
            end.advance_to(cursors[c].end());

            if (is_block(start, end)) {
                edits += 2;
            } else {
                while (start.position < end.position) {
                    edits += 1;

                    end_of_line(&start);
                    if (start.position >= end.position) {
                        break;
                    }
                    start.advance();
                }
            }
        }

        transaction.init(edits, 0);

        uint64_t offset = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            // If the start is at the start of the line and the end is at the start of a different
            // line then the lines inbetween are commented using //. In every other case we want to
            // insert /* at the start and */ at the end.

            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
            Contents_Iterator end = start;
            end.advance_to(cursors[c].end());

            if (is_block(start, end)) {
                bool space_start, space_end;

                if (start.at_bob()) {
                    space_start = true;
                } else {
                    Contents_Iterator s = start;
                    s.retreat();
                    space_start = !isspace(s.get());
                }
                if (end.at_eob()) {
                    space_end = true;
                } else {
                    space_end = !isspace(end.get());
                }

                Edit edit_start;
                if (space_start) {
                    edit_start.value = SSOStr::from_constant(" /* ");
                } else {
                    edit_start.value = SSOStr::from_constant("/* ");
                }
                edit_start.position = start.position + offset;
                offset += edit_start.value.len();
                edit_start.flags = Edit::INSERT_AFTER_POSITION;
                transaction.push(edit_start);

                Edit edit_end;
                if (space_end) {
                    edit_end.value = SSOStr::from_constant(" */ ");
                } else {
                    edit_end.value = SSOStr::from_constant(" */");
                }
                edit_end.position = end.position + offset;
                offset += edit_end.value.len();
                edit_end.flags = Edit::INSERT;
                transaction.push(edit_end);
            } else {
                Contents_Iterator s2 = start;
                uint64_t start_offset = 0;
                bool set_offset = false;
                while (s2.position < end.position) {
                    uint64_t p = s2.position;
                    forward_through_whitespace(&s2);
                    if (!set_offset || s2.position - p < start_offset) {
                        start_offset = s2.position - p;
                        set_offset = true;
                    }

                    end_of_line(&s2);
                    if (s2.position >= end.position) {
                        break;
                    }
                    s2.advance();
                }

                while (start.position < end.position) {
                    if (set_offset) {
                        start.advance(start_offset);
                    } else {
                        forward_through_whitespace(&start);
                    }

                    Edit edit;
                    edit.value = SSOStr::from_constant("// ");
                    edit.position = start.position + offset;
                    offset += 3;
                    edit.flags = Edit::INSERT_AFTER_POSITION;
                    transaction.push(edit);

                    end_of_line(&start);
                    if (start.position >= end.position) {
                        break;
                    }
                    start.advance();
                }
            }
        }
    } else {
        transaction.init(cursors.len, 0);

        SSOStr value = SSOStr::from_constant("// ");

        uint64_t offset = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
            start_of_line_text(&start);

            Edit edit;
            edit.value = value;
            edit.position = start.position + offset;
            offset += 3;
            edit.flags = Edit::INSERT;
            transaction.push(edit);
        }
    }

    transaction.commit(buffer);
}

}
}
