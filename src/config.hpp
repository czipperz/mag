#pragma once

namespace cz {
struct Str;
}

namespace mag {

struct Key_Map;
struct Theme;
struct Mode;

Key_Map create_key_map();
Theme create_theme();
Mode get_mode(cz::Str file_name);

}
