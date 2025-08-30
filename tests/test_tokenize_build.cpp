#include "syntax/tokenize_build.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("tokenize_build error url") {
    Test_Runner tr;
    tr.setup(R"(command
src/custom/config-2.cpp:1190:39: error: message
 1190 |             buffer->mode.next_token = yntax::build_next_token;
      |                                       ^~~~~
)");
    tr.set_tokenizer(syntax::build_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 20);
    CHECK(tokens[0] == Test_Runner::TToken{"command", {0, 7, Token_Type::SEARCH_COMMAND}});
    CHECK(tokens[1] ==
          Test_Runner::TToken{"src/custom/config-2.cpp:1190:39", {8, 39, Token_Type::LINK_HREF}});
    CHECK(tokens[2] == Test_Runner::TToken{":", {39, 40, Token_Type::PUNCTUATION}});
    CHECK(tokens[3] == Test_Runner::TToken{"error", {41, 46, Token_Type::IDENTIFIER}});
    CHECK(tokens[4] == Test_Runner::TToken{":", {46, 47, Token_Type::PUNCTUATION}});
    CHECK(tokens[5] == Test_Runner::TToken{"message", {48, 55, Token_Type::IDENTIFIER}});
    CHECK(tokens[6] == Test_Runner::TToken{"1190", {57, 61, Token_Type::IDENTIFIER}});
    CHECK(tokens[7] == Test_Runner::TToken{"buffer", {75, 81, Token_Type::IDENTIFIER}});
    CHECK(tokens[8] == Test_Runner::TToken{"-", {81, 82, Token_Type::PUNCTUATION}});
    CHECK(tokens[9] == Test_Runner::TToken{">", {82, 83, Token_Type::PUNCTUATION}});
    CHECK(tokens[10] == Test_Runner::TToken{"mode", {83, 87, Token_Type::IDENTIFIER}});
    CHECK(tokens[11] == Test_Runner::TToken{".", {87, 88, Token_Type::PUNCTUATION}});
    CHECK(tokens[12] == Test_Runner::TToken{"next_token", {88, 98, Token_Type::IDENTIFIER}});
    CHECK(tokens[13] == Test_Runner::TToken{"=", {99, 100, Token_Type::PUNCTUATION}});
    CHECK(tokens[14] == Test_Runner::TToken{"yntax", {101, 106, Token_Type::IDENTIFIER}});
    CHECK(tokens[15] == Test_Runner::TToken{"::", {106, 108, Token_Type::PUNCTUATION}});
    CHECK(tokens[16] ==
          Test_Runner::TToken{"build_next_token", {108, 124, Token_Type::IDENTIFIER}});
    CHECK(tokens[17] == Test_Runner::TToken{";", {124, 125, Token_Type::PUNCTUATION}});
    CHECK(tokens[18] == Test_Runner::TToken{"^", {171, 172, Token_Type::PUNCTUATION}});
    CHECK(tokens[19] == Test_Runner::TToken{"~~~~", {172, 176, Token_Type::PUNCTUATION}});
}
