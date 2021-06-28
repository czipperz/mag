#pragma once

#include <cz/string.hpp>
#include "buffer.hpp"
#include "client.hpp"
#include "editor.hpp"

namespace mag {
namespace prose {

bool copy_buffer_directory(Editor*, Client*, const Buffer* buffer, cz::String* out);
bool copy_version_control_directory(Editor* editor, Client* client, const Buffer* buffer, cz::String* directory);

}
}
