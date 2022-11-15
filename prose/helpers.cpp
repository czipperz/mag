#include "helpers.hpp"

#include <cz/working_directory.hpp>
#include "version_control/version_control.hpp"

namespace mag {
namespace prose {

bool copy_buffer_directory(Editor*, Client* client, const Buffer* buffer, cz::String* out) {
    if (buffer->directory.len > 0) {
        out->reserve(cz::heap_allocator(), buffer->directory.len + 1);
        out->append(buffer->directory);
        out->null_terminate();
        return true;
    } else {
        if (!cz::get_working_directory(cz::heap_allocator(), out)) {
            client->show_message("No current directory");
            return false;
        }
        return true;
    }
}

bool copy_version_control_directory(Editor* editor,
                                    Client* client,
                                    const Buffer* buffer,
                                    cz::String* directory) {
    if (!version_control::get_root_directory(buffer->directory, cz::heap_allocator(), directory)) {
        client->show_message("No version control repository found");
        return false;
    }

    directory->reserve(cz::heap_allocator(), 2);
    if (!directory->ends_with('/'))
        directory->push('/');
    directory->null_terminate();
    return true;
}

}
}
