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
Key_Map* directory_key_map();
Mode get_mode(cz::Str file_name);

}
