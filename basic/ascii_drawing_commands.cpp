#include "basic/ascii_drawing_commands.hpp"

#include "command_macros.hpp"
#include "movement.hpp"
#include "transaction.hpp"

namespace mag {
namespace ascii_drawing {

REGISTER_COMMAND(command_draw_box);
void command_draw_box(Editor* editor, Command_Source source) {
    constexpr uint64_t box_padding = 4;

    WITH_SELECTED_BUFFER(source.client);
    if (!window->show_marks) {
        source.client->show_message("Select a region please");
        return;
    }

    Contents_Iterator start =
        buffer->contents.iterator_at(window->cursors[window->selected_cursor].start());
    Contents_Iterator end = start;
    end.advance_to(window->cursors[window->selected_cursor].end());

    if (at_start_of_line(start) && at_start_of_line(end)) {
        if (start.position == end.position) {
            source.client->show_message("Select a region please");
            return;
        }

        uint64_t min_line_length = -1;
        uint64_t max_line_length = 0;
        for (Contents_Iterator it = start; it.position < end.position;) {
            uint64_t sol = it.position;
            end_of_line(&it);
            max_line_length = std::max(max_line_length, it.position - sol);
            min_line_length = std::min(min_line_length, it.position - sol);
            it.advance();
        }

        uint64_t offset = 0;
        Transaction transaction = {};
        transaction.init(buffer);
        CZ_DEFER(transaction.drop());

        cz::String spacer_line = {};
        spacer_line.reserve(transaction.value_allocator(), max_line_length + 2 * box_padding + 3);
        spacer_line.push('+');
        spacer_line.push_many('-', max_line_length + 2 * box_padding);
        spacer_line.push('+');
        spacer_line.push('\n');
        transaction.push(
            {SSOStr::from_constant(spacer_line), start.position + offset, Edit::INSERT});
        offset += spacer_line.len;

        cz::String empty_line = {};
        empty_line.reserve(transaction.value_allocator(), max_line_length + 2 * box_padding + 3);
        empty_line.push('|');
        empty_line.push_many(' ', max_line_length + 2 * box_padding);
        empty_line.push('|');
        empty_line.push('\n');
        transaction.push(
            {SSOStr::from_constant(empty_line), start.position + offset, Edit::INSERT});
        offset += empty_line.len;

        SSOStr left_chunk;
        {
            static_assert(box_padding + 1 <= SSOStr::MAX_SHORT_LEN,
                          "Required for optimization purposes");
            char builder[SSOStr::MAX_SHORT_LEN];
            builder[0] = '|';
            memset(builder + 1, ' ', box_padding);
            left_chunk = SSOStr::from_constant({builder, box_padding + 1});
        }

        cz::String right_chunk = {};
        right_chunk.reserve(transaction.value_allocator(),
                            max_line_length - min_line_length + box_padding + 1);
        right_chunk.push_many(' ', max_line_length - min_line_length + box_padding);
        right_chunk.push('|');

        for (Contents_Iterator it = start; it.position < end.position;) {
            transaction.push({left_chunk, it.position + offset, Edit::INSERT});
            offset += left_chunk.len();

            uint64_t sol = it.position;
            end_of_line(&it);
            uint64_t removed = it.position - sol - min_line_length;
            transaction.push({SSOStr::from_constant(right_chunk.slice_start(removed)),
                              it.position + offset, Edit::INSERT});
            offset += right_chunk.len - removed;

            it.advance();
        }

        transaction.push({SSOStr::from_constant(empty_line), end.position + offset, Edit::INSERT});
        offset += empty_line.len;
        transaction.push({SSOStr::from_constant(spacer_line), end.position + offset, Edit::INSERT});
        offset += spacer_line.len;

        transaction.commit(source.client);
    } else {
        source.client->show_message("Unsupported region");
    }
}

}
}
