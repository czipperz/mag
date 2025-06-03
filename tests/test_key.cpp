#include <czt/test_base.hpp>

#include <cz/string.hpp>
#include "core/key.hpp"
#include "test_runner.hpp"

using namespace mag;

namespace cz {
static void append(cz::Allocator allocator, cz::String* string, Key key) {
    string->reserve(allocator, stringify_key_max_size);
    stringify_key(string, key);
}
}

TEST_CASE("stringify_keys simple") {
    const Key keys[] = {{Modifiers::ALT, 'a'},
                        {0, 'h'},
                        {0, 'e'},
                        {0, 'l'},
                        {0, 'l'},
                        {0, 'o'},
                        {0, ' '},
                        {0, 'w'},
                        {0, 'o'},
                        {0, 'r'},
                        {0, 'l'},
                        {0, 'd'},
                        {Modifiers::ALT | Modifiers::SHIFT, Key_Code::F3},
                        {0, '\t'},
                        {0, '\n'}};

    cz::String string = {};
    CZ_DEFER(string.drop(cz::heap_allocator()));
    stringify_keys(cz::heap_allocator(), &string, keys);
    REQUIRE(string == "A-a 'hello world' A-S-F3 TAB ENTER");

    cz::Vector<Key> keys_out = {};
    CZ_DEFER(keys_out.drop(cz::heap_allocator()));
    CHECK(parse_keys(cz::heap_allocator(), &keys_out, string) == (int64_t)string.len);
    CHECK(keys_out.as_const_slice() == keys);
}

TEST_CASE("stringify_keys escaping keys") {
    const Key keys[] = {
        {0, 'E'}, {0, 'N'}, {0, 'T'}, {0, 'E'}, {0, 'R'}, {0, Key_Code::SCROLL_DOWN},
        {0, 'S'}, {0, 'P'}, {0, 'A'}, {0, 'C'}, {0, 'E'},
    };

    cz::String string = {};
    CZ_DEFER(string.drop(cz::heap_allocator()));
    stringify_keys(cz::heap_allocator(), &string, keys);
    REQUIRE(string == "'ENTER' SCROLL_DOWN 'SPACE'");

    cz::Vector<Key> keys_out = {};
    CZ_DEFER(keys_out.drop(cz::heap_allocator()));
    CHECK(parse_keys(cz::heap_allocator(), &keys_out, string) == (int64_t)string.len);
    CHECK(keys_out.as_const_slice() == keys);
}

TEST_CASE("stringify_keys escaping single quotes") {
    const Key keys[] = {
        {0, 'c'}, {0, '\''}, {0, 's'}, {0, ' '}, {0, 'x'},
    };

    cz::String string = {};
    CZ_DEFER(string.drop(cz::heap_allocator()));
    stringify_keys(cz::heap_allocator(), &string, keys);
    REQUIRE(string == "'c''s x'");

    cz::Vector<Key> keys_out = {};
    CZ_DEFER(keys_out.drop(cz::heap_allocator()));
    CHECK(parse_keys(cz::heap_allocator(), &keys_out, string) == (int64_t)string.len);
    CHECK(keys_out.as_const_slice() == keys);
}

TEST_CASE("parse_keys intern whitespace to prevent silly errors") {
    const Key keys[] = {{0, '\n'}, {0, '\t'}};

    cz::Str string = "'\n\t'";

    cz::Vector<Key> keys_out = {};
    CZ_DEFER(keys_out.drop(cz::heap_allocator()));
    CHECK(parse_keys(cz::heap_allocator(), &keys_out, string) == (int64_t)string.len);
    CHECK(keys_out.as_const_slice() == keys);
}
