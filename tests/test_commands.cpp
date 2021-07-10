#include <czt/test_base.hpp>

#include "basic/commands.hpp"
#include "basic/shift_commands.hpp"
#include "test_runner.hpp"

using namespace cz;
using namespace mag;
using namespace mag::basic;

TEST_CASE("forward char bob") {
    Test_Runner tr;
    tr.setup("|ABC\n12\n3456\n");
    tr.run(command_forward_char);
    CHECK(tr.stringify() == "A|BC\n12\n3456\n");
}

TEST_CASE("forward char eob") {
    Test_Runner tr;
    tr.setup("ABC\n12\n3456\n|");
    tr.run(command_forward_char);
    CHECK(tr.stringify() == "ABC\n12\n3456\n|");
}

TEST_CASE("backward char bob") {
    Test_Runner tr;
    tr.setup("|ABC\n12\n3456\n");
    tr.run(command_backward_char);
    CHECK(tr.stringify() == "|ABC\n12\n3456\n");
}

TEST_CASE("backward char eob") {
    Test_Runner tr;
    tr.setup("ABC\n12\n3456\n|");
    tr.run(command_backward_char);
    CHECK(tr.stringify() == "ABC\n12\n3456|\n");
}

TEST_CASE("start of line bob does nothing") {
    Test_Runner tr;
    tr.setup("|ABC\n12\n3456\n");
    tr.run(command_start_of_line);
    CHECK(tr.stringify() == "|ABC\n12\n3456\n");
}

TEST_CASE("start of line at start of line middle of buffer stays in place") {
    Test_Runner tr;
    tr.setup("ABC\n|12\n3456\n");
    tr.run(command_start_of_line);
    CHECK(tr.stringify() == "ABC\n|12\n3456\n");
}

TEST_CASE("start of line at middle of line start of buffer goes to start of line") {
    Test_Runner tr;
    tr.setup("A|BC\n12\n3456\n");
    tr.run(command_start_of_line);
    CHECK(tr.stringify() == "|ABC\n12\n3456\n");
}

TEST_CASE("start of line start of second line first character newline") {
    Test_Runner tr;
    tr.setup("\n|ABC\n");
    tr.run(command_start_of_line);
    CHECK(tr.stringify() == "\n|ABC\n");
}

TEST_CASE("end of line eob does nothing") {
    Test_Runner tr;
    tr.setup("ABC\n12\n3456\n|");
    tr.run(command_end_of_line);
    CHECK(tr.stringify() == "ABC\n12\n3456\n|");
}

TEST_CASE("end of line at end of line middle of buffer stays in place") {
    Test_Runner tr;
    tr.setup("ABC|\n12\n3456\n");
    tr.run(command_end_of_line);
    CHECK(tr.stringify() == "ABC|\n12\n3456\n");
}

TEST_CASE("end of line at middle of line middle of buffer goes to end of line") {
    Test_Runner tr;
    tr.setup("ABC\n|12\n3456\n");
    tr.run(command_end_of_line);
    CHECK(tr.stringify() == "ABC\n12|\n3456\n");
}

TEST_CASE("shift line forward normal case") {
    Test_Runner tr;
    tr.setup("abc\ndef\ng|hi\njkl\n");
    tr.run(command_shift_line_forward);
    CHECK(tr.stringify() == "abc\ndef\njkl\ng|hi\n");
}

TEST_CASE("shift line forward no trailing newline") {
    Test_Runner tr;
    tr.setup("abc\nd|ef\nghi");
    tr.run(command_shift_line_forward);
    CHECK(tr.stringify() == "abc\nghi\nd|ef");
}

TEST_CASE("shift line forward two lines no trailing newline") {
    Test_Runner tr;
    tr.setup("a|bc\ndef");
    tr.run(command_shift_line_forward);
    CHECK(tr.stringify() == "def\na|bc");
}

TEST_CASE("shift line backward normal case") {
    Test_Runner tr;
    tr.setup("abc\ndef\ng|hi\njkl\n");
    tr.run(command_shift_line_backward);
    CHECK(tr.stringify() == "abc\ng|hi\ndef\njkl\n");
}

TEST_CASE("shift line backward no trailing newline") {
    Test_Runner tr;
    tr.setup("abc\ndef\ng|hi");
    tr.run(command_shift_line_backward);
    CHECK(tr.stringify() == "abc\ng|hi\ndef");
}

TEST_CASE("shift line backward two lines no trailing newline") {
    Test_Runner tr;
    tr.setup("abc\nd|ef");
    tr.run(command_shift_line_backward);
    CHECK(tr.stringify() == "d|ef\nabc");
}

TEST_CASE("command_transpose_words basic cases") {
    SECTION("empty buffer") {
        Test_Runner tr;
        tr.setup("|");
        tr.run(command_transpose_words);
        CHECK(tr.stringify() == "|");
    }
    SECTION("one word before") {
        Test_Runner tr;
        tr.setup("word|");
        tr.run(command_transpose_words);
        CHECK(tr.stringify() == "word|");
    }
    SECTION("one word after") {
        Test_Runner tr;
        tr.setup("|word");
        tr.run(command_transpose_words);
        CHECK(tr.stringify() == "|word");
    }
}

TEST_CASE("command_transpose_words normal cases") {
    SECTION("two words") {
        Test_Runner tr;
        tr.setup("word1 | word2");
        tr.run(command_transpose_words);
        CHECK(tr.stringify() == "word2  word1|");
    }
    SECTION("in middle of word pretends at end of word") {
        Test_Runner tr;
        tr.setup("word1 wor|d2 word3 word4");
        tr.run(command_transpose_words);
        CHECK(tr.stringify() == "word1 word3 word2| word4");
    }
    SECTION("at start of word does before and after") {
        Test_Runner tr;
        tr.setup("word1 |word2");
        tr.run(command_transpose_words);
        CHECK(tr.stringify() == "word2 word1|");
    }
    SECTION("at end of word does before and after") {
        Test_Runner tr;
        tr.setup("word1| word2");
        tr.run(command_transpose_words);
        CHECK(tr.stringify() == "word2 word1|");
    }
}
