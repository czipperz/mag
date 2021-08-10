#pragma once

#include <cz/string.hpp>

namespace mag {
struct Client;

namespace xclip {

bool get_clipboard(void*, cz::Allocator allocator, cz::String* text);
bool set_clipboard(void*, cz::Str text);

bool use_xclip_clipboard(Client* client);

}
}
