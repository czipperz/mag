#include "key.hpp"

#include <cz/char_type.hpp>
#include <cz/string.hpp>

namespace mag {

bool Key::parse(Key* key, cz::Str description) {
    size_t i = 0;

    // Parse key modifiers.
    key->modifiers = 0;
    if (i + 1 < description.len && description[i] == 'C' && description[i + 1] == '-') {
        key->modifiers |= CONTROL;
        i += 2;
    }
    if (i + 1 < description.len && description[i] == 'A' && description[i + 1] == '-') {
        key->modifiers |= ALT;
        i += 2;
    }
    if (i + 1 < description.len && description[i] == 'S' && description[i + 1] == '-') {
        key->modifiers |= SHIFT;
        i += 2;
    }

    /// @AddKeyCode if this is refactored change the documentation.
    // Parse key code.
    cz::Str d = description.slice_start(i);
    if (d == "SPACE") {
        key->code = ' ';
    } else if (d == "BACKSPACE") {
        key->code = Key_Code::BACKSPACE;
    } else if (d == "INSERT") {
        key->code = Key_Code::INSERT;
    } else if (d == "DELETE") {
        key->code = Key_Code::DELETE_;
    } else if (d == "HOME") {
        key->code = Key_Code::HOME;
    } else if (d == "END") {
        key->code = Key_Code::END;
    } else if (d == "PAGE_UP") {
        key->code = Key_Code::PAGE_UP;
    } else if (d == "PAGE_DOWN") {
        key->code = Key_Code::PAGE_DOWN;
    } else if (d == "UP") {
        key->code = Key_Code::UP;
    } else if (d == "DOWN") {
        key->code = Key_Code::DOWN;
    } else if (d == "LEFT") {
        key->code = Key_Code::LEFT;
    } else if (d == "RIGHT") {
        key->code = Key_Code::RIGHT;
    } else if (d == "MOUSE3") {
        key->code = Key_Code::MOUSE3;
    } else if (d == "MOUSE4") {
        key->code = Key_Code::MOUSE4;
    } else if (d == "MOUSE5") {
        key->code = Key_Code::MOUSE5;
    } else if (d == "SCROLL_UP_ONE") {
        key->code = Key_Code::SCROLL_UP_ONE;
    } else if (d == "SCROLL_DOWN_ONE") {
        key->code = Key_Code::SCROLL_DOWN_ONE;
    } else if (d == "SCROLL_UP") {
        key->code = Key_Code::SCROLL_UP;
    } else if (d == "SCROLL_DOWN") {
        key->code = Key_Code::SCROLL_DOWN;
    } else if (d == "SCROLL_LEFT") {
        key->code = Key_Code::SCROLL_LEFT;
    } else if (d == "SCROLL_RIGHT") {
        key->code = Key_Code::SCROLL_RIGHT;
    } else if (d.len == 1) {
        key->code = d[0];
    } else {
        return false;
    }

    return true;
}

void stringify_key(cz::String* prefix, Key key) {
    if (key.modifiers & Modifiers::CONTROL) {
        prefix->append("C-");
    }
    if (key.modifiers & Modifiers::ALT) {
        prefix->append("A-");
    }
    if (key.modifiers & Modifiers::SHIFT) {
        if (key.code <= UCHAR_MAX && cz::is_lower(key.code)) {
            prefix->push(cz::to_upper(key.code));
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
        KEY_CODE_CASE(INSERT);
        CASE(Key_Code::DELETE_, "DELETE");
        KEY_CODE_CASE(HOME);
        KEY_CODE_CASE(END);
        KEY_CODE_CASE(PAGE_UP);
        KEY_CODE_CASE(PAGE_DOWN);

        KEY_CODE_CASE(UP);
        KEY_CODE_CASE(DOWN);
        KEY_CODE_CASE(LEFT);
        KEY_CODE_CASE(RIGHT);

        KEY_CODE_CASE(MOUSE3);
        KEY_CODE_CASE(MOUSE4);
        KEY_CODE_CASE(MOUSE5);

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
        prefix->push((char)key.code);
    }
}

}
