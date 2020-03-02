#include "file.hpp"

#include <cz/defer.hpp>
#include <cz/fs/read_to_string.hpp>
#include <cz/path.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"

namespace mag {

void open_file(Editor* editor, Client* client, cz::Str user_path) {
    if (user_path.len == 0) {
        Message message = {};
        message.tag = Message::SHOW;
        message.text = "File path must not be empty";
        client->show_message(message);
        return;
    }

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::path::make_absolute(user_path, cz::heap_allocator(), &path);
    if (path[path.len() - 1] == '/') {
        path.pop();
        path.null_terminate();
    }

    cz::Option<cz::Str> name = cz::path::name_component(path);
    CZ_ASSERT(name.is_present);

    Buffer_Id buffer_id = editor->create_buffer(name.value, cz::path::directory_component(path));

    FILE* file = fopen(path.buffer(), "r");
    if (file) {
        CZ_DEFER(fclose(file));
        // If it exists read in the buffer
        WITH_BUFFER(buffer, buffer_id, {
            cz::String contents = {};
            CZ_DEFER(contents.drop(cz::heap_allocator()));
            cz::fs::read_to_string(cz::heap_allocator(), &contents, file);
            buffer->contents.insert(0, contents);
        });
    }

    client->_selected_window->v.unified_id = buffer_id;
}

}
