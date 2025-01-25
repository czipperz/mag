#include "syntax/tokenize_markdown.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("tokenize_markdown list") {
    Test_Runner tr;
    tr.setup("* hello world*\n");
    tr.set_tokenizer(syntax::md_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 3);
    CHECK(tokens[0] == Test_Runner::TToken{"*", {0, 1, Token_Type::PUNCTUATION}});
    CHECK(tokens[1] == Test_Runner::TToken{"hello", {2, 7, Token_Type::DEFAULT}});
    CHECK(tokens[2] == Test_Runner::TToken{"world*", {8, 14, Token_Type::DEFAULT}});
}

TEST_CASE("tokenize_markdown bold/italics simple") {
    Test_Runner tr;
    tr.setup(
        "*hello world*\n"
        "**hello world**\n");
    tr.set_tokenizer(syntax::md_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 2);
    CHECK(tokens[0] == Test_Runner::TToken{"*hello world*", {0, 13, Token_Type::PROCESS_ITALICS}});
    CHECK(tokens[1] == Test_Runner::TToken{"**hello world**", {14, 29, Token_Type::PROCESS_BOLD}});
}

TEST_CASE("tokenize_markdown bold/italics middle of line") {
    Test_Runner tr;
    tr.setup(
        "x *y* z\n"
        "x **y** z\n");
    tr.set_tokenizer(syntax::md_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 6);
    CHECK(tokens[0] == Test_Runner::TToken{"x", {0, 1, Token_Type::DEFAULT}});
    CHECK(tokens[1] == Test_Runner::TToken{"*y*", {2, 5, Token_Type::PROCESS_ITALICS}});
    CHECK(tokens[2] == Test_Runner::TToken{"z", {6, 7, Token_Type::DEFAULT}});
    CHECK(tokens[3] == Test_Runner::TToken{"x", {8, 9, Token_Type::DEFAULT}});
    CHECK(tokens[4] == Test_Runner::TToken{"**y**", {10, 15, Token_Type::PROCESS_BOLD}});
    CHECK(tokens[5] == Test_Runner::TToken{"z", {16, 17, Token_Type::DEFAULT}});
}
