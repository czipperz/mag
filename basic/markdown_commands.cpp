#include "markdown_commands.hpp"

#include "buffer.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "reformat_commands.hpp"
#include "match.hpp"
#include "movement.hpp"

namespace mag {
namespace markdown {

void command_reformat_paragraph(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);

    // If we're at a starting line, don't allow ourselves
    // to be reformatted as part of a different block.
    start_of_line_text(&iterator);
    if (looking_at(iterator, "* ")) {
        basic::reformat_at(buffer, iterator, "* ", "  ");
        return;
    }
    if (looking_at(iterator, "- ")) {
        basic::reformat_at(buffer, iterator, "- ", "  ");
        return;
    }
    if (looking_at(iterator, "+ ")) {
        basic::reformat_at(buffer, iterator, "+ ", "  ");
        return;
    }

    // Check when we're at a trailing line.
    if (basic::reformat_at(buffer, iterator, "* ", "  ")) {
        return;
    }
    if (basic::reformat_at(buffer, iterator, "- ", "  ")) {
        return;
    }
    if (basic::reformat_at(buffer, iterator, "+ ", "  ")) {
        return;
    }

    // Backup: format as a paragraph.
    if (basic::reformat_at(buffer, iterator, "", "")) {
        return;
    }
}

}
}
