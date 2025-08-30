#include "syntax/tokenize_build.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("tokenize_build raw string literals") {
    Test_Runner tr;
    tr.setup("command\nsrc/syntax-/tokenize_build.cpp:75:23: error: message\n");
    tr.set_tokenizer(syntax::build_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 6);
    CHECK(tokens[0] == Test_Runner::TToken{"command", {0, 7, Token_Type::SEARCH_COMMAND}});
    CHECK(tokens[1] == Test_Runner::TToken{"src/syntax-/tokenize_build.cpp:75:23", {8, 44, Token_Type::LINK_HREF}});
    CHECK(tokens[2] == Test_Runner::TToken{":", {44, 45, Token_Type::PUNCTUATION}});
    CHECK(tokens[3] == Test_Runner::TToken{"error", {46, 51, Token_Type::IDENTIFIER}});
    CHECK(tokens[4] == Test_Runner::TToken{":", {51, 52, Token_Type::PUNCTUATION}});
    CHECK(tokens[5] == Test_Runner::TToken{"message", {53, 60, Token_Type::IDENTIFIER}});
}
