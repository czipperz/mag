#include "cpp_commands.hpp"

#include <cz/char_type.hpp>
#include "command_macros.hpp"
#include "editor.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "reformat_commands.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace cpp {

static bool is_block(Contents_Iterator start, Contents_Iterator end) {
    // If the start is at the start of the line and the end is at the start of a different
    // line then the lines inbetween are commented using //. In every other case we want to
    // insert /* at the start and */ at the end.

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

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    if (window->show_marks) {
        uint64_t offset = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
            Contents_Iterator end = start;
            end.advance_to(cursors[c].end());

            if (is_block(start, end)) {
                bool space_start, space_end;

                // Don't add an extra space outside the comment if there is already one there.
                if (start.at_bob()) {
                    space_start = true;
                } else {
                    Contents_Iterator s = start;
                    s.retreat();
                    space_start = !cz::is_space(s.get());
                }
                if (end.at_eob()) {
                    space_end = true;
                } else {
                    space_end = !cz::is_space(end.get());
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
                // We want the line comments to line up even if the lines being commented have
                // different amounts of indentation.  So we first find the minimum amount of
                // indentation on the lines (start_offset).
                uint64_t start_offset = 0;
                bool set_offset = false;
                for (Contents_Iterator s2 = start; s2.position < end.position;) {
                    // If we're not at an empty line then count the indentation.
                    if (s2.get() != '\n') {
                        uint64_t column = 0;
                        for (;; s2.advance()) {
                            char ch = s2.get();
                            // Reached end of indentation.
                            if (!cz::is_space(ch)) {
                                break;
                            }

                            uint64_t after = char_visual_columns(buffer->mode, ch, column);
                            // This line is more indented than the previous line; this
                            // may happen if we hit a tab.  If so then we may want to
                            // use the column before the tab as the start_offset.
                            if (set_offset && after > start_offset) {
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
                if (set_offset && buffer->mode.use_tabs) {
                    start_offset -= (start_offset % buffer->mode.tab_width);
                }

                while (start.position < end.position) {
                    if (start.get() == '\n') {
                        if (set_offset) {
                            uint64_t tabs, spaces;
                            analyze_indent(buffer->mode, start_offset, &tabs, &spaces);

                            cz::String string = {};
                            string.reserve(transaction.value_allocator(), tabs + spaces + 2);
                            string.push_many('\t', tabs);
                            string.push_many(' ', spaces);
                            string.append("//");

                            Edit insert_indent;
                            insert_indent.value = SSOStr::from_constant(string);
                            insert_indent.position = start.position + offset;
                            offset += string.len();
                            insert_indent.flags = Edit::INSERT_AFTER_POSITION;
                            transaction.push(insert_indent);

                            if (insert_indent.value.is_short()) {
                                string.drop(transaction.value_allocator());
                            }
                        } else {
                            Edit insert;
                            insert.value = SSOStr::from_constant("//");
                            insert.position = start.position + offset;
                            offset += 2;
                            insert.flags = Edit::INSERT_AFTER_POSITION;
                            transaction.push(insert);
                        }
                        goto cont;
                    } else if (set_offset) {
                        go_to_visual_column(buffer->mode, &start, start_offset);
                    }

                    Edit edit;
                    edit.value = SSOStr::from_constant("// ");
                    edit.position = start.position + offset;
                    offset += 3;
                    edit.flags = Edit::INSERT_AFTER_POSITION;
                    transaction.push(edit);

                cont:
                    end_of_line(&start);
                    forward_char(&start);
                }
            }
        }
    } else {
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

    transaction.commit();
}

void command_reformat_comment(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);

    if (basic::reformat_at(buffer, iterator, "// ", "// ")) {
        return;
    }

    if (basic::reformat_at(buffer, iterator, "/// ", "/// ")) {
        return;
    }

    if (basic::reformat_at(buffer, iterator, "//! ", "//! ")) {
        return;
    }

    if (basic::reformat_at(buffer, iterator, "/* ", " * ")) {
        return;
    }

    if (basic::reformat_at(buffer, iterator, "/* ", "   ")) {
        return;
    }
}

void command_reformat_comment_block_only(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);

    if (basic::reformat_at(buffer, iterator, "/* ", " * ")) {
        return;
    }

    if (basic::reformat_at(buffer, iterator, "/* ", "   ")) {
        return;
    }
}

static void change_indirection(Buffer* buffer, cz::Slice<Cursor> cursors, bool increase) {
    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    Contents_Iterator iterator = buffer->contents.start();
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        // Go to start of token.
        bool before_start = false;
        Contents_Iterator start = iterator;
        backward_char(&start);
        while (!start.at_bob()) {
            char ch = start.get();
            if (ch == '_' || cz::is_alnum(ch)) {
                start.retreat();
            } else {
                before_start = true;
                break;
            }
        }

        // Go to the end of the token.
        Contents_Iterator end = iterator;
        while (!end.at_eob()) {
            char ch = end.get();
            if (ch == '_' || cz::is_alnum(ch)) {
                end.advance();
            } else {
                break;
            }
        }

        if (looking_at(end, ".")) {
            if (increase) {
                // `a.b` becomes `a->b`.
                Edit remove_dot;
                remove_dot.value = SSOStr::from_char('.');
                remove_dot.position = end.position + offset;
                remove_dot.flags = Edit::REMOVE_AFTER_POSITION;
                transaction.push(remove_dot);

                Edit insert_arrow;
                insert_arrow.value = SSOStr::from_constant("->");
                insert_arrow.position = end.position + offset;
                insert_arrow.flags = Edit::INSERT_AFTER_POSITION;
                transaction.push(insert_arrow);

                offset += 1;
            } else {
                // `a.b` becomes `(&a).b`.
                Edit insert_start;
                insert_start.value = SSOStr::from_constant("(&");
                insert_start.position = start.position + before_start + offset;
                offset += 2;
                insert_start.flags = Edit::INSERT;
                transaction.push(insert_start);

                Edit insert_end;
                insert_end.value = SSOStr::from_char(')');
                insert_end.position = end.position + offset;
                offset += 1;
                insert_end.flags = Edit::INSERT_AFTER_POSITION;
                transaction.push(insert_end);
            }
        } else if (looking_at(end, "->")) {
            if (increase) {
                // `a->b` becomes `(*a)->b`.
                Edit insert_start;
                insert_start.value = SSOStr::from_constant("(*");
                insert_start.position = start.position + before_start + offset;
                offset += 2;
                insert_start.flags = Edit::INSERT;
                transaction.push(insert_start);

                Edit insert_end;
                insert_end.value = SSOStr::from_char(')');
                insert_end.position = end.position + offset;
                offset += 1;
                insert_end.flags = Edit::INSERT_AFTER_POSITION;
                transaction.push(insert_end);
            } else {
                // `a->b` becomes `a.b`.
                Edit remove_arrow;
                remove_arrow.value = SSOStr::from_constant("->");
                remove_arrow.position = end.position + offset;
                remove_arrow.flags = Edit::REMOVE_AFTER_POSITION;
                transaction.push(remove_arrow);

                Edit insert_dot;
                insert_dot.value = SSOStr::from_char('.');
                insert_dot.position = end.position + offset;
                insert_dot.flags = Edit::INSERT_AFTER_POSITION;
                transaction.push(insert_dot);

                offset -= 1;
            }
        } else {
            if (increase) {
                if (start.get() == '&') {
                    // `&a` -> `a`.
                    Edit remove_ampersand;
                    remove_ampersand.value = SSOStr::from_char('&');
                    remove_ampersand.position = start.position + offset;
                    offset -= 1;
                    remove_ampersand.flags = Edit::REMOVE;
                    transaction.push(remove_ampersand);
                    continue;
                }

                // `(*a)->b` becomes `(**a)->b` or `a + b` becomes `*a + b`.
                Edit insert_star;
                insert_star.value = SSOStr::from_char('*');
                insert_star.position = start.position + before_start + offset;
                offset += 1;
                insert_star.flags = Edit::INSERT;
                transaction.push(insert_star);
            } else {
                // `(*a)->b` becomes `a->b`, `*a + b` becomes `a + b`, and `a + b` becomes `&a + b`.
                if (start.get() != '*') {
                    // `a + b` becomes `&a + b` or `&a + b` becomes `&&a + b`.
                    Edit insert_ampersand;
                    insert_ampersand.value = SSOStr::from_char('&');
                    insert_ampersand.position = start.position + before_start + offset;
                    offset += 1;
                    insert_ampersand.flags = Edit::INSERT;
                    transaction.push(insert_ampersand);
                    continue;
                }

                Contents_Iterator start2 = start;
                if (start2.at_bob()) {
                // `*a + b` becomes `a + b`.
                just_remove_star:
                    Edit remove_star;
                    remove_star.value = SSOStr::from_char('*');
                    remove_star.position = start.position + offset;
                    offset -= 1;
                    remove_star.flags = Edit::REMOVE;
                    transaction.push(remove_star);
                    continue;
                }

                start2.retreat();
                if (start2.get() != '(') {
                    // `**a` becomes `*a` or `x = *a` becomes `x = a`.
                    goto just_remove_star;
                }

                if (end.at_eob() || end.get() != ')') {
                    // `(*a + b)` becomes `(a + b)`
                    goto just_remove_star;
                }

                // `(*a)` becomes `a`.
                Edit remove_start;
                remove_start.value = SSOStr::from_constant("(*");
                remove_start.position = start2.position + offset;
                offset -= 2;
                remove_start.flags = Edit::REMOVE;
                transaction.push(remove_start);

                Edit remove_end;
                remove_end.value = SSOStr::from_char(')');
                remove_end.position = end.position + offset;
                offset -= 1;
                remove_end.flags = Edit::REMOVE_AFTER_POSITION;
                transaction.push(remove_end);
                continue;
            }
        }
    }

    transaction.commit();
}

void command_make_direct(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    change_indirection(buffer, window->cursors, false);
}

void command_make_indirect(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    change_indirection(buffer, window->cursors, true);
}

}
}
