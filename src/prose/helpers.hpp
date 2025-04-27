#pragma once

#include <cz/string.hpp>
#include "core/buffer.hpp"
#include "core/client.hpp"

namespace mag {
namespace prose {

bool copy_buffer_directory(Client* client, const Buffer* buffer, cz::String* out);
bool copy_version_control_directory(Client* client, const Buffer* buffer, cz::String* directory);

}
}
