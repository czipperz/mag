#include "directory_commands.hpp"

#include "client.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "contents.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "movement.hpp"

namespace mag {

void command_directory_open_path(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        Contents_Iterator start = buffer->contents.iterator_at(buffer->cursors[0].point);
        Contents_Iterator end = start;
        start_of_line(buffer, &start);
        end_of_line(buffer, &end);

        if (start.position < end.position) {
            SSOStr str = buffer->contents.slice(cz::heap_allocator(), start.position, end.position);
            CZ_DEFER(str.drop(cz::heap_allocator()));
            open_file(editor, source.client, str.as_str());
        }
    });
}

}
