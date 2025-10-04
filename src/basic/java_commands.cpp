#include "java_commands.hpp"

#include "core/command_macros.hpp"
#include "core/movement.hpp"
#include "prose/find_file.hpp"
#include "prose/helpers.hpp"
#include "prose/search.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_java_open_token_at_point);
void command_java_open_token_at_point(Editor* editor, Command_Source source) {
    cz::String directory = {};

    SSOStr token = {};
    CZ_DEFER(token.drop(cz::heap_allocator()));

    {
        WITH_CONST_SELECTED_NORMAL_BUFFER(source.client);

        // TODO handle import lines.
        if (!get_token_at_position_contents(buffer, window->cursors[window->selected_cursor].point,
                                            &token)) {
            source.client->show_message("Cursor is not positioned at a token");
            return;
        }

        if (!prose::copy_version_control_directory(source.client, buffer->directory, &directory)) {
            return;
        }
    }

    if (token.as_str().len > 0 && cz::is_upper(token.as_str()[0])) {
        cz::Heap_String query = cz::format("%", token.as_str(), ".java$");
        CZ_DEFER(query.drop());
        prose::find_file(source.client, "Find file in version control: ", query, directory);
    } else {
        prose::run_search(source.client, editor, directory.buffer, token.as_str(),
                          /*query_word=*/false);
    }
}

}
}
