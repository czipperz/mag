#include "key.hpp"

#include <cz/char_type.hpp>
#include <cz/string.hpp>

namespace mag {

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
