#include "syntax/tokenize_cplusplus.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("tokenize_cplusplus") {
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
