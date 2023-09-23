#include "syntax/tokenize_markdown.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("tokenize_markdown") {
    Test_Runner tr;
    tr.setup("* hello world*\n*hello world*\n**hello world**");
    tr.set_tokenizer(syntax::md_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 5);
    CHECK(tokens[0] == Test_Runner::TToken{"*", {0, 1, Token_Type::PUNCTUATION}});
    CHECK(tokens[1] == Test_Runner::TToken{"hello world", {2, 13, Token_Type::DEFAULT}});
    CHECK(tokens[2] == Test_Runner::TToken{"*", {13, 14, Token_Type::DEFAULT}});
    CHECK(tokens[3] == Test_Runner::TToken{"*hello world*", {15, 28, Token_Type::PROCESS_ITALICS}});
    CHECK(tokens[4] == Test_Runner::TToken{"**hello world**", {29, 44, Token_Type::PROCESS_BOLD}});
}
