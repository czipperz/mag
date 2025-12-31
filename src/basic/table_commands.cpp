#include "table_commands.hpp"

#include <cz/util.hpp>
#include "core/command_macros.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"

namespace mag {
namespace basic {

static void rfind_start_of_table(Contents_Iterator* start) {
    while (1) {
        start_of_line(start);

        // Rough heuristic that every line in a table starts with '|'.
        if (!looking_at(*start, '|')) {
            Contents_Iterator it2 = *start;
            end_of_line(&it2);
            if (it2.at_eob())
                break;
            it2.advance();
            *start = it2;
            break;
        }

        if (start->at_bob())
            break;
        start->retreat();
    }
}

static void find_all_pipes(Contents_Iterator it,
                           cz::Vector<uint64_t>* pipe_positions,
                           cz::Vector<size_t>* line_pipe_index,
                           cz::Vector<bool>* lines_needing_additional_trailing_pipe) {
    line_pipe_index->reserve(cz::heap_allocator(), 1);
    line_pipe_index->push(0);

    while (looking_at(it, '|')) {
        pipe_positions->reserve(cz::heap_allocator(), 1);
        pipe_positions->push(it.position);
        it.advance();

        Contents_Iterator eol = it;
        end_of_line(&eol);

        while (1) {
            bool found = find_before(&it, eol.position, '|');
            if (!found)
                break;

            pipe_positions->reserve(cz::heap_allocator(), 1);
            pipe_positions->push(it.position);
            it.advance();
        }

        line_pipe_index->reserve(cz::heap_allocator(), 1);
        line_pipe_index->push(pipe_positions->len);

        lines_needing_additional_trailing_pipe->reserve(cz::heap_allocator(), 1);
        lines_needing_additional_trailing_pipe->push(pipe_positions->last() + 1 < eol.position);

        it = eol;
        forward_char(&it);
    }
}

static uint64_t get_max_pipes_per_line(cz::Slice<uint64_t> line_pipe_index,
                                       cz::Slice<bool> lines_needing_additional_trailing_pipe) {
    uint64_t max_pipes_per_line = 0;
    for (size_t i = 1; i < line_pipe_index.len; ++i) {
        max_pipes_per_line =
            cz::max(max_pipes_per_line, line_pipe_index[i] - line_pipe_index[i - 1] +
                                            lines_needing_additional_trailing_pipe[i - 1]);
    }
    return max_pipes_per_line;
}

static bool look_for_any_non_dash_characters_and_go_to_end_of_line(Contents_Iterator* it) {
    while (!it->at_eob()) {
        char ch = it->get();
        if (ch == '\n')
            break;
        if (ch != '|' && ch != '-') {
            end_of_line(it);
            return false;
        }
        it->advance();
    }
    return true;
}

static void calculate_desired_widths_for_each_column(Contents_Iterator it,
                                                     cz::Slice<uint64_t> pipe_positions,
                                                     cz::Slice<size_t> line_pipe_index,
                                                     size_t max_pipes_per_line,
                                                     cz::Vector<uint64_t>* actual_widths,
                                                     cz::Vector<uint64_t>* desired_widths) {
    actual_widths->reserve_exact(cz::heap_allocator(), max_pipes_per_line - 1);
    for (size_t i = 1; i < max_pipes_per_line; ++i) {
        actual_widths->push(0);
    }
    desired_widths->reserve_exact(cz::heap_allocator(), max_pipes_per_line - 1);
    for (size_t i = 1; i < max_pipes_per_line; ++i) {
        desired_widths->push(0);
    }

    for (size_t l = 0; l < line_pipe_index.len - 1; ++l) {
        size_t base_index = line_pipe_index[l];
        size_t end_index = line_pipe_index[l + 1];
        Contents_Iterator it2 = it;

        // Look for any non-dash characters in the line.
        bool all_dashes = look_for_any_non_dash_characters_and_go_to_end_of_line(&it);
        if (all_dashes) {
            forward_char(&it);
            continue;  // don't count all dash lines
        }

        for (size_t i = 1; i < max_pipes_per_line; ++i) {
            uint64_t start =
                (base_index + i - 1 < end_index ? pipe_positions[base_index + i - 1] + 1
                                                : it.position);
            uint64_t end = (base_index + i < end_index ? pipe_positions[base_index + i]  //
                                                       : it.position);

            uint64_t actual_width = end - start + 1;
            if (actual_width > (*actual_widths)[i - 1])
                (*actual_widths)[i - 1] = actual_width;

            bool has_starting_space = false;
            if (base_index + i - 1 < end_index) {
                it2.advance_to(start);
                has_starting_space = looking_at(it2, ' ');
            }

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

            uint64_t desired_width = end - start + 1 + !has_starting_space;
            if (desired_width > (*desired_widths)[i - 1])
                (*desired_widths)[i - 1] = desired_width;
        }
        forward_char(&it);
    }
}

static void allocate_strings_of_biggest_width(cz::Slice<uint64_t> actual_widths,
                                              cz::Slice<uint64_t> desired_widths,
                                              cz::Allocator allocator,
                                              cz::String* spaces,
                                              cz::String* dashes) {
    // Find the biggest desired width.
    uint64_t biggest_width = 0;
    CZ_DEBUG_ASSERT(actual_widths.len == desired_widths.len);
    for (size_t i = 0; i < desired_widths.len; ++i) {
        if (actual_widths[i] > biggest_width + desired_widths[i])
            biggest_width = actual_widths[i] - desired_widths[i];
        if (desired_widths[i] > biggest_width)
            biggest_width = desired_widths[i];
    }

    spaces->reserve_exact(allocator, biggest_width + 1);
    spaces->push_many(' ', biggest_width);
    spaces->push('|');

    dashes->reserve_exact(allocator, biggest_width + 1);
    dashes->push_many('-', biggest_width);
    dashes->push('|');
}

static void create_edits(Contents_Iterator it,
                         cz::Slice<uint64_t> pipe_positions,
                         cz::Slice<size_t> line_pipe_index,
                         size_t max_pipes_per_line,
                         cz::Slice<uint64_t> actual_widths,
                         cz::Slice<uint64_t> desired_widths,
                         Transaction* transaction) {
    // Allocate a string of the biggest desired width.
    cz::String spaces = {}, dashes = {};
    allocate_strings_of_biggest_width(actual_widths, desired_widths, transaction->value_allocator(),
                                      &spaces, &dashes);

    // Align the columns.
    // Note: assumes the first pipe is aligned for all the lines.
    uint64_t offset = 0;
    for (size_t l = 0; l < line_pipe_index.len - 1; ++l) {
        size_t base_index = line_pipe_index[l];
        size_t end_index = line_pipe_index[l + 1];
        Contents_Iterator it2 = it;

        // Look for any non-dash characters in the line.
        bool all_dashes = look_for_any_non_dash_characters_and_go_to_end_of_line(&it);
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

            bool has_starting_space = false;
            if (base_index + i - 1 < end_index) {
                it2.advance_to(start);
                has_starting_space = looking_at(it2, ' ');
            }
            if (!has_starting_space && !all_dashes) {
                ++width;
                Edit edit;
                edit.flags = Edit::INSERT;
                edit.position = start + offset;
                edit.value = SSOStr::from_constant(" ");
                transaction->push(edit);
                offset += edit.value.len();
            }

            if (width > desired) {
                // Make the padding string.
                uint64_t padding = width - desired;
                cz::Str str = base_string.slice_end(padding);

                // Remove padding.
                Edit edit;
                edit.flags = Edit::REMOVE;
                edit.position = end + offset - str.len;
                edit.value = SSOStr::from_constant(str);
                transaction->push(edit);

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
                transaction->push(edit);

                offset += str.len;
            }
        }

        forward_char(&it);
    }
}

REGISTER_COMMAND(command_realign_table);
void command_realign_table(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    // Assume for now that all cursors are in the same table.
    Contents_Iterator start =
        buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);
    rfind_start_of_table(&start);

    cz::Vector<uint64_t> pipe_positions = {};
    CZ_DEFER(pipe_positions.drop(cz::heap_allocator()));
    cz::Vector<size_t> line_pipe_index = {};
    CZ_DEFER(line_pipe_index.drop(cz::heap_allocator()));
    cz::Vector<bool> lines_needing_additional_trailing_pipe = {};
    CZ_DEFER(lines_needing_additional_trailing_pipe.drop(cz::heap_allocator()));

    find_all_pipes(start, &pipe_positions, &line_pipe_index,
                   &lines_needing_additional_trailing_pipe);

    size_t max_pipes_per_line =
        get_max_pipes_per_line(line_pipe_index, lines_needing_additional_trailing_pipe);
    if (max_pipes_per_line == 0) {
        source.client->show_message("Couldn't find the table to realign");
        return;
    }

    // Calculate the maximum actual and desired width of each column.
    cz::Vector<uint64_t> actual_widths = {};
    CZ_DEFER(actual_widths.drop(cz::heap_allocator()));
    cz::Vector<uint64_t> desired_widths = {};
    CZ_DEFER(desired_widths.drop(cz::heap_allocator()));
    calculate_desired_widths_for_each_column(start, pipe_positions, line_pipe_index,
                                             max_pipes_per_line, &actual_widths, &desired_widths);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    create_edits(start, pipe_positions, line_pipe_index, max_pipes_per_line, actual_widths,
                 desired_widths, &transaction);

    transaction.commit(source.client);
}

}
}
