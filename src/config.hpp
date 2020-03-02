#pragma once

namespace cz {
struct Str;
}

namespace mag {

struct Key_Map;
struct Theme;
struct Tokenizer;

Key_Map create_key_map();
Theme create_theme();
Tokenizer get_tokenizer(cz::Str file_name);

}
