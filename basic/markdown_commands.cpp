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

REGISTER_COMMAND(command_reformat_paragraph);
void command_reformat_paragraph(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
    reformat_at(source.client, buffer, iterator);
}

void reformat_at(Client *client, Buffer *buffer, Contents_Iterator iterator) {
    start_of_line_text(&iterator);

    // Don't reformat title lines.
    if (looking_at(iterator, "#")) {
        return;
    }

    cz::Str rejected_patterns[] = {"#", "* ", "- ", "+ "};
    if (basic::reformat_at(client, buffer, iterator, "* ", "  ", rejected_patterns)) {
        return;
    }
    if (basic::reformat_at(client, buffer, iterator, "- ", "  ", rejected_patterns)) {
        return;
    }
    if (basic::reformat_at(client, buffer, iterator, "+ ", "  ", rejected_patterns)) {
        return;
    }

    // Backup: format as a paragraph.
    if (basic::reformat_at(client, buffer, iterator, "", "")) {
        return;
    }
}

}
}
