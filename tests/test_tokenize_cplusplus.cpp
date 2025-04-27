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
    tr.setup("/// ```\n"
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
