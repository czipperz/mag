#include "syntax/tokenize_cplusplus.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("tokenize_cplusplus raw string literals") {
    Test_Runner tr;
    tr.setup(
        "R\"(hello ) \" world)\";\n"
        "uR\"div(hello )\" world)div\";\n"
        "u8R\"\"\"(hello )\" world)\"\"\";\n");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 9);
    CHECK(tokens[0] == Test_Runner::TToken{"R", {0, 1, Token_Type::KEYWORD}});
    CHECK(tokens[1] == Test_Runner::TToken{"\"(hello ) \" world)\"", {1, 20, Token_Type::STRING}});
    CHECK(tokens[2] == Test_Runner::TToken{";", {20, 21, Token_Type::PUNCTUATION}});
    CHECK(tokens[3] == Test_Runner::TToken{"uR", {22, 24, Token_Type::KEYWORD}});
    CHECK(tokens[4] ==
          Test_Runner::TToken{"\"div(hello )\" world)div\"", {24, 48, Token_Type::STRING}});
    CHECK(tokens[5] == Test_Runner::TToken{";", {48, 49, Token_Type::PUNCTUATION}});
    CHECK(tokens[6] == Test_Runner::TToken{"u8R", {50, 53, Token_Type::KEYWORD}});
    CHECK(tokens[7] ==
          Test_Runner::TToken{"\"\"\"(hello )\" world)\"\"\"", {53, 75, Token_Type::STRING}});
    CHECK(tokens[8] == Test_Runner::TToken{";", {75, 76, Token_Type::PUNCTUATION}});
}

TEST_CASE("tokenize_cplusplus raw string literal prefixes are normal idents if no string") {
    Test_Runner tr;
    tr.setup("R L LR u8 u8R u uR U UR");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 9);
    for (size_t i = 0; i < tokens.len; ++i) {
        INFO("i: " << i);
        CHECK(tokens[i].token.type != Token_Type::STRING);
    }
}

TEST_CASE("tokenize_cplusplus /* inside ///```") {
    Test_Runner tr;
    tr.setup(
        "/// ```\n"
        "/// hi /* bye\n"
        "/// ```\n"
        "after");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 9);
    CHECK(tokens[0] == Test_Runner::TToken{"/// ", {0, 4, Token_Type::DOC_COMMENT}});
    CHECK(tokens[1] == Test_Runner::TToken{"```", {4, 7, Token_Type::OPEN_PAIR}});
    CHECK(tokens[2] == Test_Runner::TToken{"///", {8, 11, Token_Type::DOC_COMMENT}});
    CHECK(tokens[3] == Test_Runner::TToken{"hi", {12, 14, Token_Type::IDENTIFIER}});
    CHECK(tokens[4] == Test_Runner::TToken{"/* bye", {15, 21, Token_Type::COMMENT}});
    CHECK(tokens[5] == Test_Runner::TToken{"///", {22, 25, Token_Type::DOC_COMMENT}});
    CHECK(tokens[6] == Test_Runner::TToken{" ", {25, 26, Token_Type::COMMENT}});
    CHECK(tokens[7] == Test_Runner::TToken{"```", {26, 29, Token_Type::CLOSE_PAIR}});
    CHECK(tokens[8] == Test_Runner::TToken{"after", {30, 35, Token_Type::IDENTIFIER}});
}

TEST_CASE("tokenize_cplusplus multiline string inside ///```") {
    Test_Runner tr;
    tr.setup(
        "/// ```\n"
        "/// hi \"inside1\\n\\\n"
        "/// inside2\" bye\n"
        "/// ```\n"
        "after");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 11);
    CHECK(tokens[0] == Test_Runner::TToken{"/// ", {0, 4, Token_Type::DOC_COMMENT}});
    CHECK(tokens[1] == Test_Runner::TToken{"```", {4, 7, Token_Type::OPEN_PAIR}});
    CHECK(tokens[2] == Test_Runner::TToken{"///", {8, 11, Token_Type::DOC_COMMENT}});
    CHECK(tokens[3] == Test_Runner::TToken{"hi", {12, 14, Token_Type::IDENTIFIER}});
    CHECK(tokens[4] == Test_Runner::TToken{"\"inside1\\n\\", {15, 26, Token_Type::STRING}});
    CHECK(tokens[5] == Test_Runner::TToken{"///", {27, 30, Token_Type::DOC_COMMENT}});
    CHECK(tokens[6] == Test_Runner::TToken{" inside2\"", {30, 39, Token_Type::STRING}});
    CHECK(tokens[7] == Test_Runner::TToken{"bye", {40, 43, Token_Type::IDENTIFIER}});
    CHECK(tokens[8] == Test_Runner::TToken{"///", {44, 47, Token_Type::DOC_COMMENT}});
    CHECK(tokens[9] == Test_Runner::TToken{"```", {48, 51, Token_Type::CLOSE_PAIR}});
    CHECK(tokens[10] == Test_Runner::TToken{"after", {52, 57, Token_Type::IDENTIFIER}});
}

TEST_CASE("tokenize_cplusplus markdown in /// comment") {
    Test_Runner tr;
    tr.setup(
        "/// # Header\n"
        "/// * List1\n"
        "///   - List2\n"
        "///     + List3\n"
        "/// Not a * list.\n");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 13);
    CHECK(tokens[0] == Test_Runner::TToken{"/// ", {0, 4, Token_Type::DOC_COMMENT}});
    CHECK(tokens[1] == Test_Runner::TToken{"#", {4, 5, Token_Type::PUNCTUATION}});
    CHECK(tokens[2] == Test_Runner::TToken{" Header", {5, 12, Token_Type::TITLE}});
    CHECK(tokens[3] == Test_Runner::TToken{"/// ", {13, 17, Token_Type::DOC_COMMENT}});
    CHECK(tokens[4] == Test_Runner::TToken{"*", {17, 18, Token_Type::PUNCTUATION}});
    CHECK(tokens[5] == Test_Runner::TToken{" List1", {18, 24, Token_Type::DOC_COMMENT}});
    CHECK(tokens[6] == Test_Runner::TToken{"///   ", {25, 31, Token_Type::DOC_COMMENT}});
    CHECK(tokens[7] == Test_Runner::TToken{"-", {31, 32, Token_Type::PUNCTUATION}});
    CHECK(tokens[8] == Test_Runner::TToken{" List2", {32, 38, Token_Type::DOC_COMMENT}});
    CHECK(tokens[9] == Test_Runner::TToken{"///     ", {39, 47, Token_Type::DOC_COMMENT}});
    CHECK(tokens[10] == Test_Runner::TToken{"+", {47, 48, Token_Type::PUNCTUATION}});
    CHECK(tokens[11] == Test_Runner::TToken{" List3", {48, 54, Token_Type::DOC_COMMENT}});
    CHECK(tokens[12] ==
          Test_Runner::TToken{"/// Not a * list.", {55, 72, Token_Type::DOC_COMMENT}});
}

TEST_CASE("tokenize_cplusplus markdown in /** comment") {
    Test_Runner tr;
    tr.setup(
        "/** # Header\n"
        "    * List1\n"
        "      - List2\n"
        "        + List3\n"
        "    Not a * list. */\n");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 8);
    CHECK(tokens[0] == Test_Runner::TToken{"/** ", {0, 4, Token_Type::DOC_COMMENT}});
    CHECK(tokens[1] == Test_Runner::TToken{"#", {4, 5, Token_Type::PUNCTUATION}});
    CHECK(tokens[2] == Test_Runner::TToken{" Header", {5, 12, Token_Type::TITLE}});
    CHECK(tokens[3] ==
          Test_Runner::TToken{"\n    * List1\n      ", {12, 31, Token_Type::DOC_COMMENT}});
    CHECK(tokens[4] == Test_Runner::TToken{"-", {31, 32, Token_Type::PUNCTUATION}});
    CHECK(tokens[5] == Test_Runner::TToken{" List2\n        ", {32, 47, Token_Type::DOC_COMMENT}});
    CHECK(tokens[6] == Test_Runner::TToken{"+", {47, 48, Token_Type::PUNCTUATION}});
    CHECK(tokens[7] ==
          Test_Runner::TToken{" List3\n    Not a * list. */", {48, 75, Token_Type::DOC_COMMENT}});
}

TEST_CASE("tokenize_cplusplus markdown in /** comment with * continuations") {
    Test_Runner tr;
    tr.setup(
        "/** # Header\n"
        " *  * List1\n"
        " *    - List2\n"
        " *      + List3\n"
        " *  Not a * list. */\n");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 10);
    CHECK(tokens[0] == Test_Runner::TToken{"/** ", {0, 4, Token_Type::DOC_COMMENT}});
    CHECK(tokens[1] == Test_Runner::TToken{"#", {4, 5, Token_Type::PUNCTUATION}});
    CHECK(tokens[2] == Test_Runner::TToken{" Header", {5, 12, Token_Type::TITLE}});
    CHECK(tokens[3] == Test_Runner::TToken{"\n *  ", {12, 17, Token_Type::DOC_COMMENT}});
    CHECK(tokens[4] == Test_Runner::TToken{"*", {17, 18, Token_Type::PUNCTUATION}});
    CHECK(tokens[5] == Test_Runner::TToken{" List1\n *    ", {18, 31, Token_Type::DOC_COMMENT}});
    CHECK(tokens[6] == Test_Runner::TToken{"-", {31, 32, Token_Type::PUNCTUATION}});
    CHECK(tokens[7] == Test_Runner::TToken{" List2\n *      ", {32, 47, Token_Type::DOC_COMMENT}});
    CHECK(tokens[8] == Test_Runner::TToken{"+", {47, 48, Token_Type::PUNCTUATION}});
    CHECK(tokens[9] ==
          Test_Runner::TToken{" List3\n *  Not a * list. */", {48, 75, Token_Type::DOC_COMMENT}});
}
