#include "cpp_commands.hpp"

#include <ctype.h>
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

/// Append a key to the string.  Assumes there is enough space (reserve at least 21 characters in
/// advance).
///
/// @AddKeyCode If this is refactored make sure to update the documentation in `Key_Code`.
static void append_key(cz::String* prefix, Key key) {
    if (key.modifiers & Modifiers::CONTROL) {
        prefix->append("C-");
    }
    if (key.modifiers & Modifiers::ALT) {
        prefix->append("A-");
    }
    if (key.modifiers & Modifiers::SHIFT) {
        if (key.code <= UCHAR_MAX && islower(key.code)) {
            prefix->push(toupper(key.code));
            return;
        } else {
            prefix->append("S-");
        }
    }

    switch (key.code) {
#define CASE(CONDITION, STRING) \
    case CONDITION:             \
        prefix->append(STRING); \
        break
#define KEY_CODE_CASE(KEY) CASE(Key_Code::KEY, #KEY)

        KEY_CODE_CASE(BACKSPACE);

        KEY_CODE_CASE(UP);
        KEY_CODE_CASE(DOWN);
        KEY_CODE_CASE(LEFT);
        KEY_CODE_CASE(RIGHT);

        KEY_CODE_CASE(SCROLL_UP);
        KEY_CODE_CASE(SCROLL_DOWN);
        KEY_CODE_CASE(SCROLL_LEFT);
        KEY_CODE_CASE(SCROLL_RIGHT);
        KEY_CODE_CASE(SCROLL_UP_ONE);
        KEY_CODE_CASE(SCROLL_DOWN_ONE);

        CASE(' ', "SPACE");

#undef KEY_CODE_CASE

    case '\t':
        prefix->append("TAB");
        break;
    case '\n':
        prefix->append("ENTER");
        break;

    default:
        prefix->push(key.code);
    }
}

static void add_key_map(Contents* contents, cz::String* prefix, const Key_Map& key_map) {
    prefix->reserve(cz::heap_allocator(), 32);
    for (size_t i = 0; i < key_map.bindings.len(); ++i) {
        auto& binding = key_map.bindings[i];
        size_t old_len = prefix->len();
        append_key(prefix, binding.key);

        if (binding.is_command) {
            contents->append(*prefix);
            contents->append(" ");
            contents->append(binding.v.command.string);
            contents->append("\n");
        } else {
            prefix->push(' ');
            add_key_map(contents, prefix, *binding.v.map);
        }

        prefix->set_len(old_len);
    }
}

void command_dump_key_map(Editor* editor, Command_Source source) {
    Buffer_Id buffer_id;
    if (!find_temp_buffer(editor, source.client, "*key map*", &buffer_id)) {
        buffer_id = editor->create_temp_buffer("key map", {});
    }

    WITH_BUFFER(buffer_id);
    buffer->contents.remove(0, buffer->contents.len);

    cz::String prefix = {};
    CZ_DEFER(prefix.drop(cz::heap_allocator()));

    add_key_map(&buffer->contents, &prefix, editor->key_map);

    source.client->set_selected_buffer(buffer_id);
}

}
}
