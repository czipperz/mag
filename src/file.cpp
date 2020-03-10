#include "file.hpp"

#include <cz/defer.hpp>
#include <cz/fs/directory.hpp>
#include <cz/fs/read_to_string.hpp>
#include <cz/path.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "editor.hpp"

namespace mag {

static cz::Result load_file(Editor* editor, const char* path, Buffer_Id buffer_id) {
    FILE* file = fopen(path, "r");
    if (!file) {
        return cz::Result::last_error();
    }

    CZ_DEFER(fclose(file));

    WITH_BUFFER(buffer, buffer_id, {
        cz::String contents = {};
        CZ_DEFER(contents.drop(cz::heap_allocator()));
        CZ_TRY(cz::fs::read_to_string(cz::heap_allocator(), &contents, file));
        buffer->contents.insert(0, contents);
    });

    return cz::Result::ok();
}

static cz::Result load_path(Editor* editor, const char* path, Buffer_Id buffer_id) {
    // Try reading it as a directory, then if that fails read it as a file.
    cz::fs::DirectoryIterator iterator(cz::heap_allocator());
    if (iterator.create(path).is_err()) {
        return load_file(editor, path, buffer_id);
    }

    CZ_DEFER(iterator.destroy());

    WITH_BUFFER(buffer, buffer_id, {
        while (!iterator.done()) {
            buffer->contents.insert(buffer->contents.len, iterator.file());
            buffer->contents.insert(buffer->contents.len, "\n");
            CZ_TRY(iterator.advance());
        }
    });

    return cz::Result::ok();
}

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

    Buffer_Id buffer_id = editor->create_buffer(path);
    if (load_path(editor, path.buffer(), buffer_id).is_err()) {
        Message message = {};
        message.tag = Message::SHOW;
        message.text = "File not found";
        client->show_message(message);
        // Still open empty file buffer.
    }

    CZ_DEBUG_ASSERT(client->_selected_window->tag == Window::UNIFIED);
    client->_selected_window->v.unified.id = buffer_id;
    client->_selected_window->v.unified.start_position = 0;
}

bool save_contents(const Contents* contents, const char* path) {
    FILE* file = fopen(path, "w");
    if (!file) {
        return false;
    }
    CZ_DEFER(fclose(file));

    for (size_t bucket = 0; bucket < contents->buckets.len(); ++bucket) {
        fwrite(contents->buckets[bucket].elems, 1, contents->buckets[bucket].len, file);
    }

    return true;
}

}
