#include "helpers.hpp"

#include <cz/working_directory.hpp>
#include "version_control/version_control.hpp"

namespace mag {
namespace prose {

bool copy_buffer_directory(Client* client, cz::Str buffer_directory, cz::String* out) {
    if (buffer_directory.len > 0) {
        out->reserve(cz::heap_allocator(), buffer_directory.len + 1);
        out->append(buffer_directory);
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

bool copy_version_control_directory(Client* client, cz::Str buffer_directory, cz::String* out) {
    if (!version_control::get_root_directory(buffer_directory, cz::heap_allocator(), out)) {
        client->show_message("No version control repository found");
        return false;
    }

    out->reserve(cz::heap_allocator(), 2);
    if (!out->ends_with('/'))
        out->push('/');
    out->null_terminate();
    return true;
}

}
}
