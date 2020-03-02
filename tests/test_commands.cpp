#include <czt/test_base.hpp>

#include <cz/defer.hpp>
#include "command_macros.hpp"
#include "commands.hpp"
#include "server.hpp"

using namespace cz;
using namespace mag;

#define SETUP_TEST(CONTENTS)              \
    Server server = {};                   \
    CZ_DEFER(server.drop());              \
    Editor* editor = &server.editor;      \
    editor->create_buffer("*test*", {});  \
                                          \
    Client client = server.make_client(); \
    Command_Source source;                \
    source.client = &client;              \
    source.keys = {0, 0};                 \
    insert_default_contents(editor, source, CONTENTS);

static void insert_default_contents(Editor* editor, Command_Source source, cz::Str contents) {
    WITH_SELECTED_BUFFER(WITH_TRANSACTION({
        transaction.reserve(buffer->cursors.len());
        SSOStr value;
        value.init_from_constant(contents);
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {
            Edit edit;
            edit.value = value;
            edit.position = buffer->cursors[i].point;
            edit.is_insert = true;
            transaction.push(edit);
        }
    }));
}

#define STRINGIFY_BUFFER()                                                  \
    cz::String contents = buffer->contents.stringify(cz::heap_allocator()); \
    CZ_DEFER(contents.drop(cz::heap_allocator()));

TEST_CASE("forward char bob") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 0;
    });

    command_forward_char(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 1);
    });
}

TEST_CASE("forward char eob") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = buffer->contents.len();
    });

    command_forward_char(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == buffer->contents.len());
    });
}

TEST_CASE("backward char bob") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 0;
    });

    command_backward_char(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 0);
    });
}

TEST_CASE("backward char eob") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = buffer->contents.len();
    });

    command_backward_char(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == buffer->contents.len() - 1);
    });
}

TEST_CASE("start of line bob does nothing") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 0;
    });

    command_start_of_line(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 0);
    });
}

TEST_CASE("start of line at start of line middle of buffer stays in place") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 4;
    });

    command_start_of_line(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 4);
    });
}

TEST_CASE("start of line at middle of line start of buffer goes to start of line") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 1;
    });

    command_start_of_line(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 0);
    });
}

TEST_CASE("start of line start of second line first character newline") {
    SETUP_TEST("\nABC\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 1;
    });

    command_start_of_line(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "\nABC\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 1);
    });
}

TEST_CASE("end of line eob does nothing") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = buffer->contents.len();
    });

    command_end_of_line(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == buffer->contents.len());
    });
}

TEST_CASE("end of line at end of line middle of buffer stays in place") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 3;
    });

    command_end_of_line(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 3);
    });
}

TEST_CASE("end of line at middle of line middle of buffer goes to end of line") {
    SETUP_TEST("ABC\n12\n3456\n");

    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 4;
    });

    command_end_of_line(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "ABC\n12\n3456\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 6);
    });
}

TEST_CASE("shift line forward normal case") {
    SETUP_TEST("abc\ndef\nghi\njkl\n");
    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 9;  // h
    });

    command_shift_line_forward(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "abc\ndef\njkl\nghi\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 13);  // h
    });
}

TEST_CASE("shift line forward no trailing newline") {
    SETUP_TEST("abc\ndef\nghi");
    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 5;  // e
    });

    command_shift_line_forward(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "abc\nghi\ndef");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 9);  // e
    });
}

TEST_CASE("shift line forward two lines no trailing newline") {
    SETUP_TEST("abc\ndef");
    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 1;  // b
    });

    command_shift_line_forward(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "def\nabc");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 5);  // b
    });
}

TEST_CASE("shift line backward normal case") {
    SETUP_TEST("abc\ndef\nghi\njkl\n");
    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 9;  // h
    });

    command_shift_line_backward(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "abc\nghi\ndef\njkl\n");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 5);  // h
    });
}

TEST_CASE("shift line backward no trailing newline") {
    SETUP_TEST("abc\ndef\nghi");
    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 9;  // h
    });

    command_shift_line_backward(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "abc\nghi\ndef");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 5);  // h
    });
}

TEST_CASE("shift line backward two lines no trailing newline") {
    SETUP_TEST("abc\ndef");
    WITH_SELECTED_BUFFER({
        REQUIRE(buffer->cursors.len() == 1);
        buffer->cursors[0].point = 5;  // e
    });

    command_shift_line_backward(editor, source);

    WITH_SELECTED_BUFFER({
        STRINGIFY_BUFFER();
        CHECK(contents == "def\nabc");

        REQUIRE(buffer->cursors.len() == 1);
        CHECK(buffer->cursors[0].point == 1);  // e
    });
}
