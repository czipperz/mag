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
    SECTION("empty file") {
        Test_Runner tr;
        tr.setup("|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "|");
    }
}

TEST_CASE("command_recapitalize_token_to underscores at start and end are ignored") {
    SECTION("bunch of underscores in a row") {
        Test_Runner tr;
        tr.setup("___|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "___|");
    }

    SECTION("underscores start") {
        Test_Runner tr;
        tr.setup("___testRunner|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "___test_runner|");
    }
    SECTION("underscores end") {
        Test_Runner tr;
        tr.setup("testRunner___|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "test_runner___|");
    }
}

static bool entire_buffer_as_identifier(Contents_Iterator* iterator, Token* token, uint64_t*) {
    if (iterator->at_eob()) {
        return false;
    }

    token->type = Token_Type::IDENTIFIER;
    token->start = iterator->position;
    iterator->advance_to(iterator->contents->len);
    token->end = iterator->position;
    return true;
}

TEST_CASE("command_recapitalize_token_to dashes at start and end are ignored") {
    Test_Runner tr;
    tr.set_tokenizer(entire_buffer_as_identifier);

    SECTION("bunch of dashes in a row") {
        tr.setup("---|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "---|");
    }

    SECTION("dashes start") {
        tr.setup("---testRunner|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "---test_runner|");
    }
    SECTION("dashes end") {
        tr.setup("testRunner---|");
        tr.run(command_recapitalize_token_to_snake);
        CHECK(tr.stringify() == "test_runner---|");
    }
}

static bool entire_buffer_as_type(Contents_Iterator* iterator, Token* token, uint64_t*) {
    if (iterator->at_eob()) {
        return false;
    }

    token->type = Token_Type::TYPE;
    token->start = iterator->position;
    iterator->advance_to(iterator->contents->len);
    token->end = iterator->position;
    return true;
}

TEST_CASE("command_recapitalize_token_to allows type tokens") {
    Test_Runner tr;
    tr.set_tokenizer(entire_buffer_as_type);
    tr.setup("Word1Word2|");
    tr.run(command_recapitalize_token_to_snake);
    CHECK(tr.stringify() == "word1_word2|");
}

TEST_CASE("command_recapitalize_token_to multiple cursors") {
    Test_Runner tr;
    tr.setup("Word1_Word2| Word3_Word4|");
    tr.run(command_recapitalize_token_to_camel);
    CHECK(tr.stringify() == "word1Word2| word3Word4|");
}
