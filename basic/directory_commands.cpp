#include "directory_commands.hpp"

#include "client.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "contents.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "movement.hpp"

namespace mag {
namespace basic {

void command_directory_open_path(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);
        Contents_Iterator start = buffer->contents.iterator_at(window->cursors[0].point);
        Contents_Iterator end = start;
        start_of_line(&start);
        forward_char(&start);
        forward_char(&start);
        end_of_line(&end);

        if (start.position < end.position) {
            SSOStr file_name = buffer->contents.slice(cz::heap_allocator(), start, end.position);
            CZ_DEFER(file_name.drop(cz::heap_allocator()));
            cz::Str str = file_name.as_str();

            bool add_name = buffer->type != Buffer::DIRECTORY;
            path.reserve(cz::heap_allocator(),
                         buffer->directory.len() + add_name * buffer->name.len() + str.len);
            path.append(buffer->directory);
            if (add_name) {
                path.append(buffer->name);
            }
            path.append(str);
        }
    }

    if (path.len() > 0) {
        open_file(editor, source.client, path);
    }
}

}
}
