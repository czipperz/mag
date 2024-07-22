#include "test_runner.hpp"

#include "basic/markdown_commands.hpp"
#include "basic/reformat_commands.hpp"
#include "basic/cpp_commands.hpp"

using namespace mag;
using namespace mag::basic;

TEST_CASE("reformat_at markdown reformat normal paragraph") {
    // There are two paragraphs both are a single line & way too long.  The idea
    // for this test is to make sure that if your cursor is in the first
    // paragraph that only the first paragraph gets reformatted.  And if you're
    // in the second paragraph then only the second paragraph gets reformatted.
    cz::Str body =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
        "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, "
        "quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo\n"
        "\n"
        "consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse "
        "cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat "
        "non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";
    cz::Str result1 =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod\n"
        "tempor incididunt ut labore et dolore magna aliqua.  Ut enim ad minim veniam,\n"
        "quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo\n"
        "\n"
        "consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse "
        "cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat "
        "non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";
    cz::Str result2 =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
        "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, "
        "quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo\n"
        "\n"
        "consequat.  Duis aute irure dolor in reprehenderit in voluptate velit esse\n"
        "cillum dolore eu fugiat nulla pariatur.  Excepteur sint occaecat cupidatat\n"
        "non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

    size_t midpoint = body.find_index("\n\n") + 1;
    INFO("midpoint: " << midpoint);

    for (size_t i = 0; i <= body.len; ++i) {
        INFO("cursor: " << i);

        Test_Runner tr;
        tr.setup(body);

        WITH_SELECTED_BUFFER(&tr.client);
        buffer->mode.preferred_column = 80;

        // Midpoint won't match the pattern because it's at an empty line.
        bool expected = i != midpoint;

        Contents_Iterator it = buffer->contents.iterator_at(i);
        CHECK(reformat_at(&tr.client, buffer, it, "", "") == expected);
        cz::String contents = buffer->contents.stringify(tr.allocator());
        if (i < midpoint) {
            CHECK(contents == result1);
        } else if (i == midpoint) {
            CHECK(contents == body);
        } else {
            CHECK(contents == result2);
        }

        // Reformat again and make sure that we have no changes.
        it = buffer->contents.iterator_at(i);
        CHECK(reformat_at(&tr.client, buffer, it, "", "") == expected);
        CHECK(buffer->contents.stringify(tr.allocator()) == contents);
    }
}

TEST_CASE("reformat_at markdown no changes super basic") {
    cz::Str body =
        "\
- |pt1\n\
  * |pt2\n\
    + |pt3\n\
";

    Test_Runner tr;
    tr.setup(body);

    WITH_SELECTED_BUFFER(&tr.client);
    for (size_t c = 0; c < window->cursors.len; ++c) {
        INFO("cursor: " << c);

        Contents_Iterator it = buffer->contents.iterator_at(window->cursors[c].point);
        markdown::reformat_at(&tr.client, buffer, it);

        REQUIRE(tr.stringify(window, buffer) == body);
    }
}

TEST_CASE("reformat_at markdown staircase") {
    char body1[] =
        "- Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
        "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, "
        "quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo\n";
    char body2[] =
        "  - this\n"
        "    should be combined\n";
    char body3[] =
        "    - and so should\n"
        "      this!";
    char result1[] =
        "- Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod\n"
        "  tempor incididunt ut labore et dolore magna aliqua.  Ut enim ad minim veniam,\n"
        "  quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo\n";
    char result2[] = "  - this should be combined\n";
    char result3[] = "    - and so should this!";

    auto run_test_suite = [](cz::Str body, size_t start, size_t len, cz::Str result) {
        for (size_t i = start; i < start + len; ++i) {
            INFO("i: " << i);

            Test_Runner tr;
            tr.setup(body);

            WITH_SELECTED_BUFFER(&tr.client);
            buffer->mode.preferred_column = 80;

            Contents_Iterator it = buffer->contents.iterator_at(i);
            markdown::reformat_at(&tr.client, buffer, it);

            REQUIRE(buffer->contents.stringify(tr.allocator()) == result);
        }
    };

    auto test_each_paragraph = [&](cz::Str spacer) {
        cz::String body = cz::format(body1, spacer, body2, spacer, body3);
        CZ_DEFER(body.drop(cz::heap_allocator()));

        SECTION("paragraph split") {
            cz::String result = cz::format(result1, spacer, body2, spacer, body3);
            CZ_DEFER(result.drop(cz::heap_allocator()));
            run_test_suite(body, 0, strlen(body1), result);
        }

        SECTION("line combine 1") {
            cz::String result = cz::format(body1, spacer, result2, spacer, body3);
            CZ_DEFER(result.drop(cz::heap_allocator()));
            run_test_suite(body, strlen(body1) + spacer.len, strlen(body2), result);
        }

        SECTION("line combine 2") {
            cz::String result = cz::format(body1, spacer, body2, spacer, result3);
            CZ_DEFER(result.drop(cz::heap_allocator()));
            run_test_suite(body, strlen(body1) + strlen(body2) + 2 * spacer.len, strlen(body3),
                           result);
        }

        SECTION("already formatted do nothing") {
            cz::String result = cz::format(result1, spacer, result2, spacer, result3);
            CZ_DEFER(result.drop(cz::heap_allocator()));
            run_test_suite(result, 0, strlen(result1), result);
            run_test_suite(result, strlen(result1) + spacer.len, strlen(result2), result);
            run_test_suite(result, strlen(result1) + strlen(result2) + 2 * spacer.len,
                           strlen(result3), result);
        }
    };

    auto run_each = [&](cz::Str spacer) {
        SECTION("all dash body") {
            test_each_paragraph(spacer);
        }

        SECTION("mixed starting character body") {
            body1[0] = '*';
            body2[2] = '-';
            body3[4] = '+';
            result1[0] = '*';
            result2[2] = '-';
            result3[4] = '+';

            test_each_paragraph(spacer);
        }
    };

    SECTION("no spacer") {
        run_each("");
    }
    SECTION("one line spacer") {
        run_each("\n");
    }
}

// If at the first or third line, reformat into a single paragraph,
// treating the '2.' as a normal word.  If on the line starting with
// '2.', then treat the '2.' as starting a separate number paragraph.
TEST_CASE("reformat_at markdown numbered list number considered word") {
    char input[] =
        "My nephew is\n"
        "2. He is well.\n"
        "Have a good one.";
    cz::Str str_input = input;

    for (uint64_t i = 0; i < CZ_DIM(input); ++i) {
        Test_Runner tr;
        tr.setup(input);

        WITH_SELECTED_BUFFER(&tr.client);
        buffer->mode.preferred_column = 80;

        Contents_Iterator it = buffer->contents.iterator_at(i);
        markdown::reformat_at(&tr.client, buffer, it);

        if (i <= str_input.find_index('\n') || i > str_input.rfind_index('\n')) {
            REQUIRE(buffer->contents.stringify(tr.allocator()) ==
                    "My nephew is 2.  He is well.  Have a good one.");
        } else {
            REQUIRE(buffer->contents.stringify(tr.allocator()) == input);
        }
    }
}

// Tests that each of the 3 input paragraphs are reformatted
// correctly when they are selected by the cursor.
TEST_CASE("reformat_at markdown numbered list") {
    char body1[] =
        "1. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
        "incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud "
        "exercitation ullamco laboris nisi ut aliquip ex ea commodo\n";
    char body2[] =
        "a. this\n"
        "   should be combined\n";
    char body3[] =
        "3. and so should\n"
        "   this!";
    char result1[] =
        "1. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod\n"
        "   tempor incididunt ut labore et dolore magna aliqua.  Ut enim ad minim veniam,\n"
        "   quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo\n";
    char result2[] = "a. this should be combined\n";
    char result3[] = "3. and so should this!";

    cz::String body = cz::format(body1, body2, body3);
    CZ_DEFER(body.drop(cz::heap_allocator()));

    auto run_test_suite = [](cz::Str body, size_t start, size_t len, cz::Str result) {
        for (size_t i = start; i < start + len; ++i) {
            INFO("i: " << i);

            Test_Runner tr;
            tr.setup(body);

            WITH_SELECTED_BUFFER(&tr.client);
            buffer->mode.preferred_column = 80;

            Contents_Iterator it = buffer->contents.iterator_at(i);
            markdown::reformat_at(&tr.client, buffer, it);

            REQUIRE(buffer->contents.stringify(tr.allocator()) == result);
        }
    };

    SECTION("paragraph split") {
        cz::String result = cz::format(result1, body2, body3);
        CZ_DEFER(result.drop(cz::heap_allocator()));
        run_test_suite(body, 0, strlen(body1), result);
    }

    SECTION("line combine 1") {
        cz::String result = cz::format(body1, result2, body3);
        CZ_DEFER(result.drop(cz::heap_allocator()));
        run_test_suite(body, strlen(body1), strlen(body2), result);
    }

    SECTION("line combine 2") {
        cz::String result = cz::format(body1, body2, result3);
        CZ_DEFER(result.drop(cz::heap_allocator()));
        run_test_suite(body, strlen(body1) + strlen(body2), strlen(body3), result);
    }
}

TEST_CASE("reformat c++ comment") {
    const char* input =
        "\n"
        "|// Ignore return value so that if the user does like 'find' we can still handle the "
        "output by just treating the entire line as a relpath.";
    const char* output =
        "\n"
        "|// Ignore return value so that if the user does like 'find' we can still\n"
        "// handle the output by just treating the entire line as a relpath.";

    Test_Runner tr;
    tr.setup(input);

    {
        WITH_SELECTED_BUFFER(&tr.client);
        buffer->mode.preferred_column = 80;
    }

    Command_Source source = {};
    source.client = &tr.client;
    cpp::command_reformat_comment(&tr.server.editor, source);

    CHECK(tr.stringify() == output);
}
