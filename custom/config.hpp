#pragma once

#include <cz/str.hpp>

namespace mag {

struct Key_Map;
struct Theme;
struct Mode;
struct Buffer;

namespace custom {

mag::Key_Map create_key_map();
mag::Theme create_theme();
mag::Mode get_mode(const Buffer& buffer);

}
}
