#include "basic/cpp_commands.hpp"
#include "test_runner.hpp"

using namespace mag;
using namespace mag::cpp;

TEST_CASE("cpp::command_make_direct basic") {
    SECTION("ooga") {
        Test_Runner tr;
        tr.setup("ooga|");
        tr.run(command_make_direct);
        CHECK(tr.stringify() == "&ooga|");
    }
    SECTION("ooga.x") {
        Test_Runner tr;
        tr.setup("o|oga.x");
        tr.run(command_make_direct);
        CHECK(tr.stringify() == "(&o|oga).x");
    }
    SECTION("ooga->x") {
        Test_Runner tr;
        tr.setup("o|oga->x");
        tr.run(command_make_direct);
        CHECK(tr.stringify() == "o|oga.x");
    }
    SECTION("(*ooga)->x") {
        Test_Runner tr;
        tr.setup("(*o|oga)->x");
        tr.run(command_make_direct);
        CHECK(tr.stringify() == "o|oga->x");
    }
    SECTION("*x") {
        Test_Runner tr;
        tr.setup("*|x");
        tr.run(command_make_direct);
        CHECK(tr.stringify() == "|x");
    }
}

TEST_CASE("cpp::command_make_indirect basic") {
    SECTION("ooga") {
        Test_Runner tr;
        tr.setup("ooga|");
        tr.run(command_make_indirect);
        CHECK(tr.stringify() == "*ooga|");
    }
    SECTION("ooga.x") {
        Test_Runner tr;
        tr.setup("o|oga.x");
        tr.run(command_make_indirect);
        CHECK(tr.stringify() == "o|oga->x");
    }
    SECTION("ooga->x") {
        Test_Runner tr;
        tr.setup("o|oga->x");
        tr.run(command_make_indirect);
        CHECK(tr.stringify() == "(*o|oga)->x");
    }
    SECTION("(*ooga)->x") {
        Test_Runner tr;
        tr.setup("(*o|oga)->x");
        tr.run(command_make_indirect);
        CHECK(tr.stringify() == "(**o|oga)->x");
    }
    SECTION("&x") {
        Test_Runner tr;
        tr.setup("&|x");
        tr.run(command_make_indirect);
        CHECK(tr.stringify() == "|x");
    }
}
