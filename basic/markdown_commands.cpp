#include "markdown_commands.hpp"

#include "buffer.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "reformat_commands.hpp"

namespace mag {
namespace markdown {

void command_reformat_paragraph(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);

    // Don't reformat title lines.
    if (looking_at(iterator, "#")) {
        return;
    }

    // If we're at a starting line, don't allow ourselves
    // to be reformatted as part of a different block.
    start_of_line_text(&iterator);
    cz::Str rejected_patterns[] = {"#", "- ", "+ "};
    if (looking_at(iterator, "* ")) {
        basic::reformat_at(source.client, buffer, iterator, "* ", "  ", rejected_patterns);
        return;
    }
    rejected_patterns[1] = "* ";
    if (looking_at(iterator, "- ")) {
        basic::reformat_at(source.client, buffer, iterator, "- ", "  ", rejected_patterns);
        return;
    }
    rejected_patterns[2] = "- ";
    if (looking_at(iterator, "+ ")) {
        basic::reformat_at(source.client, buffer, iterator, "+ ", "  ", rejected_patterns);
        return;
    }

    // Check when we're at a trailing line.
    rejected_patterns[1] = "+ ";
    if (basic::reformat_at(source.client, buffer, iterator, "* ", "  ", rejected_patterns)) {
        return;
    }
    rejected_patterns[2] = "* ";
    if (basic::reformat_at(source.client, buffer, iterator, "- ", "  ", rejected_patterns)) {
        return;
    }
    rejected_patterns[1] = "- ";
    if (basic::reformat_at(source.client, buffer, iterator, "+ ", "  ", rejected_patterns)) {
        return;
    }

    // Backup: format as a paragraph.
    if (basic::reformat_at(source.client, buffer, iterator, "", "")) {
        return;
    }
}

}
}
