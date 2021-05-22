#include "basic/capitalization_commands.hpp"
#include "test_runner.hpp"

using namespace mag;
using namespace mag::basic;

TEST_CASE("command_recapitalize_token_to_camel one word") {
    SECTION("lower") {
        Test_Runner tr;
        tr.setup("ooga|");
        tr.run(command_recapitalize_token_to_camel);
        CHECK(tr.stringify() == "ooga|");
    }
    SECTION("upper") {
        Test_Runner tr;
        tr.setup("Ooga|");
        tr.run(command_recapitalize_token_to_camel);
        CHECK(tr.stringify() == "ooga|");
    }
    SECTION("screaming") {
        Test_Runner tr;
        tr.setup("OOGA|");
        tr.run(command_recapitalize_token_to_camel);
        CHECK(tr.stringify() == "ooga|");
    }
}

TEST_CASE("command_recapitalize_token_to two words") {
    SECTION("camel") {
        Test_Runner tr;
        tr.setup("abc_def|");
        tr.run(command_recapitalize_token_to_camel);
        CHECK(tr.stringify() == "abcDef|");
    }
    SECTION("pascal") {
        Test_Runner tr;
        tr.setup("abc_def|");
        tr.run(command_recapitalize_token_to_pascal);
        CHECK(tr.stringify() == "AbcDef|");
    }
    SECTION("snake") {
        Test_Runner tr;
        tr.setup("abcDef|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "abc_def|");
    }
    SECTION("usnake") {
        Test_Runner tr;
        tr.setup("abc_def|");
        tr.run(command_recapitalize_token_to_usnake);
        CHECK(tr.stringify() == "Abc_Def|");
    }
    SECTION("ssnake") {
        Test_Runner tr;
        tr.setup("abc_def|");
        tr.run(command_recapitalize_token_to_ssnake);
        CHECK(tr.stringify() == "ABC_DEF|");
    }
    SECTION("kebab") {
        Test_Runner tr;
        tr.setup("abc_def|");
        tr.run(command_recapitalize_token_to_kebab);
        CHECK(tr.stringify() == "abc-def|");
    }
    SECTION("ukebab") {
        Test_Runner tr;
        tr.setup("abc_def|");
        tr.run(command_recapitalize_token_to_ukebab);
        CHECK(tr.stringify() == "Abc-Def|");
    }
    SECTION("skebab") {
        Test_Runner tr;
        tr.setup("abc_def|");
        tr.run(command_recapitalize_token_to_skebab);
        CHECK(tr.stringify() == "ABC-DEF|");
    }
}

TEST_CASE("command_recapitalize_token_to parse edge cases") {
    SECTION("camel case first component one letter") {
        Test_Runner tr;
        tr.setup("aBcde|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "a_bcde|");
    }
    SECTION("screaming input one component") {
        Test_Runner tr;
        tr.setup("FILE|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "file|");
    }
    SECTION("camel case capital chain before") {
        Test_Runner tr;
        tr.setup("FILEComponent|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "file_component|");
    }
    SECTION("camel case capital chain after") {
        Test_Runner tr;
        tr.setup("ComponentFILE|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "component_file|");
    }
}
