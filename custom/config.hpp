#pragma once

#include <cz/str.hpp>

namespace mag {

struct Key_Map;
struct Theme;
struct Mode;
struct Buffer;

namespace custom {

Key_Map create_key_map();
Theme create_theme();
Mode get_mode(const Buffer& buffer);
Key_Map* window_completion_key_map();

}
}
