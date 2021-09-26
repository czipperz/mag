#include "html_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"

namespace mag {
namespace html {

REGISTER_COMMAND(command_comment);
void command_comment(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    if (window->show_marks) {
        for (size_t c = 0; c < cursors.len; ++c) {
            Contents_Iterator it = buffer->contents.iterator_at(cursors[c].start());
            Contents_Iterator end = it;
            end.advance_to(cursors[c].end());

            bool both_at_sol = true;

            Contents_Iterator it_sol = it;
            start_of_line(&it_sol);
            Contents_Iterator it_solt = it_sol;
            forward_through_whitespace(&it_solt);
            if (it.position > it_solt.position) {
                both_at_sol = false;
            }

            Contents_Iterator end_sol = end;
            start_of_line(&end_sol);
            Contents_Iterator end_solt = end_sol;
            forward_through_whitespace(&end_solt);
            if (end.position > end_solt.position) {
                both_at_sol = false;
            }

            // If the start and end of the region are at the start of a line then insert `<!--`
            // and `-->` on separate lines from the source.  Otherwise insert them inline.  If
            // the entire region is zero-width then we want to do an inline comment.
            if (both_at_sol && end.position > it.position) {
                cz::String string_start = {};
                string_start.reserve(transaction.value_allocator(),
                                     5 + it_solt.position - it_sol.position);
                string_start.append("<!--\n");
                buffer->contents.slice_into(it_sol, it_solt.position, &string_start);

                Edit insert_start;
                insert_start.value = SSOStr::from_constant(string_start);
                insert_start.position = it_solt.position + offset;
                insert_start.flags = Edit::INSERT_AFTER_POSITION;
                transaction.push(insert_start);
                offset += insert_start.value.len();
                if (insert_start.value.is_short()) {
                    string_start.drop(transaction.value_allocator());
                }

                // Get the indent for the previous line.
                end_sol.retreat();
                uint64_t end_insert_position = end_sol.position;
                start_of_line(&end_sol);
                end_solt = end_sol;
                forward_through_whitespace(&end_solt);

                cz::String string_end = {};
                string_end.reserve(transaction.value_allocator(),
                                   end_solt.position - end_sol.position + 4);
                string_end.push('\n');
                buffer->contents.slice_into(end_sol, end_solt.position, &string_end);
                string_end.append("-->");

                Edit insert_end;
                insert_end.value = SSOStr::from_constant(string_end);
                insert_end.position = end_insert_position + offset;
                insert_end.flags = Edit::INSERT;
                transaction.push(insert_end);
                offset += insert_end.value.len();
                if (insert_end.value.is_short()) {
                    string_end.drop(transaction.value_allocator());
                }
            } else {
                forward_through_whitespace(&it);

                bool insert_starting_space = false;
                if (!it.at_bob()) {
                    it.retreat();
                    insert_starting_space = !cz::is_space(it.get());
                    it.advance();
                }

                Edit insert_start;
                insert_start.value =
                    SSOStr::from_constant(insert_starting_space ? " <!-- " : "<!-- ");
                insert_start.position = it.position + offset;
                insert_start.flags = Edit::INSERT_AFTER_POSITION;
                transaction.push(insert_start);
                offset += insert_start.value.len();

                bool insert_ending_space = false;
                if (!end.at_eob()) {
                    insert_ending_space = !cz::is_space(end.get());
                }

                Edit insert_end;
                insert_end.value = SSOStr::from_constant(insert_ending_space ? " -->" : " --> ");
                insert_end.position = end.position + offset;
                insert_end.flags = Edit::INSERT;
                transaction.push(insert_end);
                offset += insert_end.value.len();
                continue;
            }
        }
    } else {
        Contents_Iterator it = buffer->contents.iterator_at(cursors[0].point);
        for (size_t c = 0; c < cursors.len; ++c) {
            it.advance_to(cursors[c].point);
            start_of_line_text(&it);

            Edit insert_start;
            insert_start.value = SSOStr::from_constant("<!-- ");
            insert_start.position = it.position + offset;
            insert_start.flags = Edit::INSERT;
            transaction.push(insert_start);
            offset += 5;

            end_of_line_text(&it);

            Edit insert_end;
            insert_end.value = SSOStr::from_constant(" -->");
            insert_end.position = it.position + offset;
            insert_end.flags = Edit::INSERT;
            transaction.push(insert_end);
            offset += 4;
        }

        // If there is only one cursor and no region selected then move to the next line.
        if (cursors.len == 1) {
            Contents_Iterator it = buffer->contents.iterator_at(cursors[0].point);
            forward_line(buffer->mode, &it);
            cursors[0].point = it.position;
        }
    }

    transaction.commit(source.client);
}

}
}
