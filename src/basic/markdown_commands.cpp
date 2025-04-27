#include "markdown_commands.hpp"

#include <cz/format.hpp>
#include "core/buffer.hpp"
#include "core/command.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "reformat_commands.hpp"

namespace mag {
namespace markdown {

static bool is_at_ordered_list_start(Contents_Iterator it, size_t* continuation_columns);

REGISTER_COMMAND(command_reformat_paragraph);
void command_reformat_paragraph(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
    reformat_at(source.client, buffer, iterator);
}

REGISTER_COMMAND(command_reformat_paragraph_or_hash_comment);
void command_reformat_paragraph_or_hash_comment(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
    start_of_line_text(&iterator);
    if (looking_at(iterator, '#')) {
        basic::reformat_at(source.client, buffer, iterator, "# ", "# ");
    } else {
        reformat_at(source.client, buffer, iterator);
    }
}

void reformat_at(Client* client, Buffer* buffer, Contents_Iterator iterator) {
    start_of_line_text(&iterator);
    uint64_t initial_position = iterator.position;
    uint64_t first_valid_line = iterator.position;

    uint64_t column = get_visual_column(buffer->mode, iterator);
    cz::Str rejected_patterns[] = {"#", "* ", "- ", "+ "};

    while (1) {
        if (looking_at(iterator, "#") || at_end_of_line(iterator)) {
            if (iterator.position == initial_position) {
                // Don't reformat title lines or empty lines.
                return;
            } else {
            vanilla:
                // No pattern has matched so far, so just reformat as a vanilla paragraph.
                iterator.advance_to(first_valid_line);
                basic::reformat_at(client, buffer, iterator, "", "");
                return;
            }
        }

        uint64_t col = get_visual_column(buffer->mode, iterator);

        // Format unordered lists.
        for (size_t i = 1; i < CZ_DIM(rejected_patterns); ++i) {
            cz::Str pat = rejected_patterns[i];
            if (iterator.position == initial_position) {
                if (looking_at(iterator, pat)) {
                    basic::reformat_at(client, buffer, iterator, pat, "  ", rejected_patterns);
                    return;
                }
            } else {
                if (col + pat.len == column && looking_at(iterator, pat)) {
                    basic::reformat_at(client, buffer, iterator, pat, "  ", rejected_patterns);
                    return;
                }
            }
        }

        // Format ordered lists.
        size_t continuation_columns;
        if (is_at_ordered_list_start(iterator, &continuation_columns)) {
            if (iterator.position == initial_position || col + continuation_columns == column) {
                cz::String number = {};
                CZ_DEFER(number.drop(cz::heap_allocator()));
                buffer->contents.slice_into(cz::heap_allocator(), iterator,
                                            iterator.position + continuation_columns, &number);

                cz::String continuation = cz::format(cz::many(' ', continuation_columns));
                CZ_DEFER(continuation.drop(cz::heap_allocator()));

                basic::reformat_at(client, buffer, iterator, number, continuation,
                                   rejected_patterns);
                return;
            }

            // Note: the only fallthrough case here is where col == column.  This only
            // happens if the cursor starts at the bottom line of the snippet below.
            // In this case we treat the number as if it was part of the paragraph as
            // a normal word.  But if the cursor is on the line starting with `2.`
            // then we always format as if it was an independent one-line paragraph.
            //
            // ```
            // My nephew is
            // 2. He is well.
            // Have a good one.
            // ```
        }

        // If at a line that isn't part of this paragraph, or reach
        // the start of the file, just fall back to vanilla reformat.
        if (col != column || iterator.position == 0) {
            goto vanilla;
        }

        first_valid_line = iterator.position;
        start_of_line(&iterator);
        backward_char(&iterator);
        start_of_line_text(&iterator);
    }
}

static bool is_at_ordered_list_start(Contents_Iterator it, size_t* continuation_columns) {
    uint64_t start = it.position;

    // We're considering an ordered list to either be a number (1, 2, 3) or a letter (a, b, c).
    if (cz::is_digit(it.get())) {
        do {
            it.advance();
        } while (!it.at_eob() && cz::is_digit(it.get()));
    } else if (cz::is_lower(it.get())) {
        it.advance();
    }

    if (!looking_at(it, ". "))
        return false;

    *continuation_columns = it.position + 2 /* strlen(". ") */ - start;
    return true;
}

}
}
