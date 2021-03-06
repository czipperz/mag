#include "helpers.hpp"

#include "version_control/version_control.hpp"

namespace mag {
namespace prose {

bool copy_buffer_directory(Editor*, Client*, const Buffer* buffer, cz::String* out) {
    if (buffer->directory.len() > 0) {
        out->reserve(cz::heap_allocator(), buffer->directory.len() + 1);
        out->append(buffer->directory);
        out->null_terminate();
    }
    return true;
}

bool copy_version_control_directory(Editor* editor,
                                    Client* client,
                                    const Buffer* buffer,
                                    cz::String* directory) {
    if (!version_control::get_root_directory(editor, client, buffer->directory.buffer(),
                                             cz::heap_allocator(), directory)) {
        client->show_message(editor, "No version control repository found");
        return false;
    }

    directory->reserve(cz::heap_allocator(), 2);
    directory->push('/');
    directory->null_terminate();
    return true;
}

}
}
