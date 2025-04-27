#include "table_commands.hpp"

#include "command_macros.hpp"
#include "match.hpp"
#include "movement.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_realign_table);
void command_realign_table(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    // Assume for now that all cursors are in the same table.
    uint64_t point = window->cursors[window->selected_cursor].point;
    Contents_Iterator start = buffer->contents.iterator_at(point);
    Contents_Iterator end = start;

    // Find start of table.
    while (1) {
        start_of_line(&start);

        // Rough heuristic that every line in a table starts with '|'.
        if (!looking_at(start, '|')) {
            Contents_Iterator it2 = start;
            end_of_line(&it2);
            if (it2.at_eob())
                break;
            it2.advance();
            start = it2;
            break;
        }

        if (start.at_bob())
            break;
        start.retreat();
    }

    // Find end of table.
    while (1) {
        end_of_line(&end);
        if (end.at_eob()) {
            // TODO handle tables without trailing newline.
            start_of_line(&end);
            break;
        }

        end.advance();
        // Rough heuristic that every line in a table starts with '|'.
        if (!looking_at(end, '|'))
            break;
    }

    cz::Vector<uint64_t> pipe_positions = {};
    CZ_DEFER(pipe_positions.drop(cz::heap_allocator()));
    cz::Vector<uint64_t> line_pipe_index = {};
    CZ_DEFER(line_pipe_index.drop(cz::heap_allocator()));
    uint64_t max_pipes_per_line = 0;

    line_pipe_index.reserve(cz::heap_allocator(), 1);
    line_pipe_index.push(0);

    // Analyze the table to find all the pipes.
    Contents_Iterator it = start;
    while (it.position < end.position) {
        CZ_DEBUG_ASSERT(looking_at(it, '|'));
        pipe_positions.reserve(cz::heap_allocator(), 1);
        pipe_positions.push(it.position);
        it.advance();

        Contents_Iterator eol = it;
        end_of_line(&eol);

        while (1) {
            bool found = find_before(&it, eol.position, '|');
            if (!found)
                break;

            pipe_positions.reserve(cz::heap_allocator(), 1);
            pipe_positions.push(it.position);
            it.advance();
        }

        line_pipe_index.reserve(cz::heap_allocator(), 1);
        line_pipe_index.push(pipe_positions.len);
        size_t num_pipes =
            line_pipe_index[line_pipe_index.len - 1] - line_pipe_index[line_pipe_index.len - 2];
        if (num_pipes > max_pipes_per_line)
            max_pipes_per_line = num_pipes;

        it = eol;
        forward_char(&it);
    }

    // Calculate the desired width of each column.
    cz::Vector<uint64_t> desired_widths = {};
    desired_widths.reserve_exact(cz::heap_allocator(), max_pipes_per_line - 1);
    for (size_t i = 1; i < max_pipes_per_line; ++i) {
        desired_widths.push(0);
    }
    it.retreat_to(start.position);
    for (size_t l = 0; l < line_pipe_index.len - 1; ++l) {
        size_t base_index = line_pipe_index[l];
        size_t end_index = line_pipe_index[l + 1];
        Contents_Iterator it2 = it;

        // Look for any non-dash characters in the line.
        bool all_dashes = true;
        while (!it.at_eob()) {
            char ch = it.get();
            if (ch == '\n')
                break;
            if (ch != '|' && ch != '-') {
                all_dashes = false;
                end_of_line(&it);
                break;
            }
            it.advance();
        }
        if (all_dashes) {
            forward_char(&it);
            continue;  // don't count all dash lines
        }

        for (size_t i = 1; i < max_pipes_per_line; ++i) {
            uint64_t start = (base_index + i - 1 < end_index ? pipe_positions[base_index + i - 1]
                                                             : it.position - 1);
            uint64_t end = (base_index + i < end_index ? pipe_positions[base_index + i]  //
                                                       : it.position);

            if (base_index + i < end_index) {
                it2.advance_to(end);
                while (!it2.at_bob()) {
                    it2.retreat();
                    if (it2.get() != ' ') {
                        it2.advance();
                        break;
                    }
                }
                end = it2.position;
            }

            uint64_t width = end - start;
            // width++;  // extra padding space
            if (width > desired_widths[i - 1])
                desired_widths[i - 1] = width;
        }
        forward_char(&it);
    }

    // Find the biggest desired width.
    uint64_t biggest_desired_width = 0;
    for (size_t i = 0; i < desired_widths.len; ++i) {
        if (desired_widths[i] > biggest_desired_width)
            biggest_desired_width = desired_widths[i];
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    // Allocate a string of the biggest desired width.
    cz::String spaces = {};
    spaces.reserve_exact(transaction.value_allocator(), biggest_desired_width + 1);
    spaces.push_many(' ', biggest_desired_width);
    spaces.push('|');
    cz::String dashes = {};
    dashes.reserve_exact(transaction.value_allocator(), biggest_desired_width + 1);
    dashes.push_many('-', biggest_desired_width);
    dashes.push('|');

    // Align the columns.
    // Note: assumes the first pipe is aligned for all the lines.
    uint64_t offset = 0;
    it.retreat_to(start.position);
    for (size_t l = 0; l < line_pipe_index.len - 1; ++l) {
        size_t base_index = line_pipe_index[l];
        size_t end_index = line_pipe_index[l + 1];

        // Look for any non-dash characters in the line.
        bool all_dashes = true;
        while (!it.at_eob()) {
            char ch = it.get();
            if (ch == '\n')
                break;
            if (ch != '|' && ch != '-') {
                all_dashes = false;
                end_of_line(&it);
                break;
            }
            it.advance();
        }
        cz::Str base_string = (all_dashes ? dashes : spaces);

        for (size_t i = 1; i < max_pipes_per_line; ++i) {
            // Find the start and end of the column.
            uint64_t start =
                (base_index + i - 1 < end_index ? pipe_positions[base_index + i - 1] + 1
                                                : it.position);
            uint64_t end = (base_index + i < end_index ? pipe_positions[base_index + i]  //
                                                       : it.position);
            bool has_pipe = (base_index + i < end_index);

            uint64_t width = end - start;
            uint64_t desired = desired_widths[i - 1];
            if (has_pipe && width == desired)
                continue;

            if (width > desired) {
                // Make the padding string.
                uint64_t padding = width - desired;
                cz::Str str = base_string.slice_end(padding);

                // Remove padding.
                Edit edit;
                edit.flags = Edit::REMOVE;
                edit.position = end + offset - str.len;
                edit.value = SSOStr::from_constant(str);
                transaction.push(edit);

                offset -= str.len;

                if (!has_pipe)
                    width = desired;
            }

            if (width < desired || !has_pipe) {
                // Make the padding string.
                uint64_t padding = desired - width;
                cz::Str str = (has_pipe ? base_string.slice_end(padding)
                                        : base_string.slice_start(base_string.len - padding - 1));

                // Insert padding.
                Edit edit;
                edit.flags = Edit::INSERT;
                edit.position = end + offset;
                edit.value = SSOStr::from_constant(str);
                transaction.push(edit);

                offset += str.len;
            }
        }

        forward_char(&it);
    }

    transaction.commit(source.client);
}

}
}
