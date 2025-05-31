#include "key.hpp"

#include <cz/char_type.hpp>
#include <cz/string.hpp>

namespace mag {

bool Key::parse(Key* key, cz::Str description) {
    size_t i = 0;

    // Parse key modifiers.
    key->modifiers = 0;
    if (i + 1 < description.len && description[i] == 'G' && description[i + 1] == '-') {
        key->modifiers |= GUI;
        i += 2;
    }
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
    } else if (d == "TAB") {
        key->code = '\t';
    } else if (d == "ENTER") {
        key->code = '\n';
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
    } else if (d == "ESCAPE") {
        key->code = Key_Code::ESCAPE;
    } else if (d == "UP") {
        key->code = Key_Code::UP;
    } else if (d == "DOWN") {
        key->code = Key_Code::DOWN;
    } else if (d == "LEFT") {
        key->code = Key_Code::LEFT;
    } else if (d == "RIGHT") {
        key->code = Key_Code::RIGHT;
    } else if (d == "F1") {
        key->code = Key_Code::F1;
    } else if (d == "F2") {
        key->code = Key_Code::F2;
    } else if (d == "F3") {
        key->code = Key_Code::F3;
    } else if (d == "F4") {
        key->code = Key_Code::F4;
    } else if (d == "F5") {
        key->code = Key_Code::F5;
    } else if (d == "F6") {
        key->code = Key_Code::F6;
    } else if (d == "F7") {
        key->code = Key_Code::F7;
    } else if (d == "F8") {
        key->code = Key_Code::F8;
    } else if (d == "F9") {
        key->code = Key_Code::F9;
    } else if (d == "F10") {
        key->code = Key_Code::F10;
    } else if (d == "F11") {
        key->code = Key_Code::F11;
    } else if (d == "F12") {
        key->code = Key_Code::F12;
    } else if (d == "MENU") {
        key->code = Key_Code::MENU;
    } else if (d == "SCROLL_LOCK") {
        key->code = Key_Code::SCROLL_LOCK;
    } else if (d == "MOUSE1") {
        key->code = Key_Code::MOUSE1;
    } else if (d == "MOUSE2") {
        key->code = Key_Code::MOUSE2;
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

void stringify_key(cz::String* string, Key key) {
    if (key.modifiers & Modifiers::GUI) {
        string->append("G-");
    }
    if (key.modifiers & Modifiers::CONTROL) {
        string->append("C-");
    }
    if (key.modifiers & Modifiers::ALT) {
        string->append("A-");
    }
    if (key.modifiers & Modifiers::SHIFT) {
        if (key.code <= UCHAR_MAX && cz::is_lower((char)key.code)) {
            string->push(cz::to_upper((char)key.code));
            return;
        } else {
            string->append("S-");
        }
    }

    /// @AddKeyCode
    switch (key.code) {
#define CASE(CONDITION, STRING) \
    case CONDITION:             \
        string->append(STRING); \
        break
#define KEY_CODE_CASE(KEY) CASE(Key_Code::KEY, #KEY)

        KEY_CODE_CASE(BACKSPACE);
        KEY_CODE_CASE(INSERT);
        CASE(Key_Code::DELETE_, "DELETE");
        KEY_CODE_CASE(HOME);
        KEY_CODE_CASE(END);
        KEY_CODE_CASE(PAGE_UP);
        KEY_CODE_CASE(PAGE_DOWN);
        KEY_CODE_CASE(ESCAPE);

        KEY_CODE_CASE(UP);
        KEY_CODE_CASE(DOWN);
        KEY_CODE_CASE(LEFT);
        KEY_CODE_CASE(RIGHT);

        KEY_CODE_CASE(F1);
        KEY_CODE_CASE(F2);
        KEY_CODE_CASE(F3);
        KEY_CODE_CASE(F4);
        KEY_CODE_CASE(F5);
        KEY_CODE_CASE(F6);
        KEY_CODE_CASE(F7);
        KEY_CODE_CASE(F8);
        KEY_CODE_CASE(F9);
        KEY_CODE_CASE(F10);
        KEY_CODE_CASE(F11);
        KEY_CODE_CASE(F12);

        KEY_CODE_CASE(MENU);
        KEY_CODE_CASE(SCROLL_LOCK);

        KEY_CODE_CASE(MOUSE1);
        KEY_CODE_CASE(MOUSE2);
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
        string->append("TAB");
        break;
    case '\n':
        string->append("ENTER");
        break;

    default:
        CZ_DEBUG_ASSERT(key.code <= UCHAR_MAX);
        CZ_DEBUG_ASSERT(cz::is_print((char)key.code));
        string->push((char)key.code);
        break;
    }
}

void stringify_keys(cz::Allocator allocator, cz::String* string, cz::Slice<const Key> keys) {
    const auto is_print = [](const Key& key) {
        return key.modifiers == 0 && key.code <= UCHAR_MAX && cz::is_print((char)key.code);
    };

    bool in_single_quotes = false;
    for (size_t i = 0; i < keys.len; ++i) {
        if (is_print(keys[i])) {
            string->reserve(allocator, 4);

            // Printable characters must be surrounded in single quotes.
            if (!in_single_quotes) {
                if (i >= 1)
                    string->push(' ');
                string->push('\'');
                in_single_quotes = true;
            }

            // Use single quotes as the escape character too to make it the only special character.
            if (keys[i].code == '\'')
                string->push('\'');
            string->push(keys[i].code);
            continue;
        }

        string->reserve(allocator, 2 + stringify_key_max_size);
        if (in_single_quotes) {
            string->push('\'');
            in_single_quotes = false;
        }
        if (i >= 1)
            string->push(' ');
        stringify_key(string, keys[i]);
    }

    if (in_single_quotes) {
        string->reserve(allocator, 1);
        string->push('\'');
    }
}

int64_t parse_keys(cz::Allocator allocator, cz::Vector<Key>* keys, cz::Str string) {
    for (size_t i = 0; i < string.len; ++i) {
        if (string[i] == '\'') {
            // Parse sequence of printable characters.
            for (++i;; ++i) {
                if (i == string.len)
                    return -(int64_t)i;  // error unterminated '
                if (!cz::is_print(string[i]))
                    return -(int64_t)i;  // error non printable character in printable section

                if (string[i] == '\'') {
                    ++i;
                    // Fallthrough unless the single quote is escaped.
                    if (i >= string.len || string[i] != '\'') {
                        break;
                    }
                }

                keys->reserve(allocator, 1);
                keys->push({0, (uint16_t)string[i]});
            }
        } else {
            // Parse modified or non-printable.
            size_t word_length = string.slice_start(i).find_index(' ');
            Key key;
            if (!Key::parse(&key, string.slice(i, i + word_length)))
                return -(int64_t)i;  // invalid key
            keys->reserve(allocator, 1);
            keys->push(key);
            i += word_length;
        }
    }
    return (int64_t)string.len;
}

}
