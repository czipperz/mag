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

TEST_CASE("tokenize_cplusplus identifier and number") {
    Test_Runner tr;
    tr.setup("$ident$ 123 ident_123");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 3);
    CHECK(tokens[0] == Test_Runner::TToken{"$ident$", {0, 7, Token_Type::IDENTIFIER}});
    CHECK(tokens[1] == Test_Runner::TToken{"123", {8, 11, Token_Type::NUMBER}});
    CHECK(tokens[2] == Test_Runner::TToken{"ident_123", {12, 21, Token_Type::IDENTIFIER}});
}

TEST_CASE("tokenize_cplusplus template braces") {
    Test_Runner tr;
    tr.setup(
        "template <class T> void f(std::vector<T<int>> my_vector) { var>>=3; var<<=3; var<=>3; "
        "bool operator<(); bool operator>();");
    tr.set_tokenizer(syntax::cpp_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 44);
    CHECK(tokens[0] == Test_Runner::TToken{"template", {0, 8, Token_Type::KEYWORD}});
    CHECK(tokens[1] == Test_Runner::TToken{"<", {9, 10, Token_Type::OPEN_PAIR}});
    CHECK(tokens[2] == Test_Runner::TToken{"class", {10, 15, Token_Type::KEYWORD}});
    CHECK(tokens[3] == Test_Runner::TToken{"T", {16, 17, Token_Type::TYPE}});
    CHECK(tokens[4] == Test_Runner::TToken{">", {17, 18, Token_Type::CLOSE_PAIR}});
    CHECK(tokens[5] == Test_Runner::TToken{"void", {19, 23, Token_Type::TYPE}});
    CHECK(tokens[6] == Test_Runner::TToken{"f", {24, 25, Token_Type::IDENTIFIER}});
    CHECK(tokens[7] == Test_Runner::TToken{"(", {25, 26, Token_Type::OPEN_PAIR}});
    CHECK(tokens[8] == Test_Runner::TToken{"std", {26, 29, Token_Type::IDENTIFIER}});
    CHECK(tokens[9] == Test_Runner::TToken{"::", {29, 31, Token_Type::PUNCTUATION}});
    CHECK(tokens[10] == Test_Runner::TToken{"vector", {31, 37, Token_Type::IDENTIFIER}});
    CHECK(tokens[11] == Test_Runner::TToken{"<", {37, 38, Token_Type::OPEN_PAIR}});
    CHECK(tokens[12] == Test_Runner::TToken{"T", {38, 39, Token_Type::IDENTIFIER}});
    CHECK(tokens[13] == Test_Runner::TToken{"<", {39, 40, Token_Type::OPEN_PAIR}});
    CHECK(tokens[14] == Test_Runner::TToken{"int", {40, 43, Token_Type::TYPE}});
    CHECK(tokens[15] == Test_Runner::TToken{">", {43, 44, Token_Type::CLOSE_PAIR}});
    CHECK(tokens[16] == Test_Runner::TToken{">", {44, 45, Token_Type::CLOSE_PAIR}});
    CHECK(tokens[17] == Test_Runner::TToken{"my_vector", {46, 55, Token_Type::IDENTIFIER}});
    CHECK(tokens[18] == Test_Runner::TToken{")", {55, 56, Token_Type::CLOSE_PAIR}});
    CHECK(tokens[19] == Test_Runner::TToken{"{", {57, 58, Token_Type::OPEN_PAIR}});
    CHECK(tokens[20] == Test_Runner::TToken{"var", {59, 62, Token_Type::IDENTIFIER}});
    CHECK(tokens[21] == Test_Runner::TToken{">>=", {62, 65, Token_Type::PUNCTUATION}});
    CHECK(tokens[22] == Test_Runner::TToken{"3", {65, 66, Token_Type::NUMBER}});
    CHECK(tokens[23] == Test_Runner::TToken{";", {66, 67, Token_Type::PUNCTUATION}});
    CHECK(tokens[24] == Test_Runner::TToken{"var", {68, 71, Token_Type::IDENTIFIER}});
    CHECK(tokens[25] == Test_Runner::TToken{"<<=", {71, 74, Token_Type::PUNCTUATION}});
    CHECK(tokens[26] == Test_Runner::TToken{"3", {74, 75, Token_Type::NUMBER}});
    CHECK(tokens[27] == Test_Runner::TToken{";", {75, 76, Token_Type::PUNCTUATION}});
    CHECK(tokens[28] == Test_Runner::TToken{"var", {77, 80, Token_Type::IDENTIFIER}});
    CHECK(tokens[29] == Test_Runner::TToken{"<=>", {80, 83, Token_Type::PUNCTUATION}});
    CHECK(tokens[30] == Test_Runner::TToken{"3", {83, 84, Token_Type::NUMBER}});
    CHECK(tokens[31] == Test_Runner::TToken{";", {84, 85, Token_Type::PUNCTUATION}});
    CHECK(tokens[32] == Test_Runner::TToken{"bool", {86, 90, Token_Type::TYPE}});
    CHECK(tokens[33] == Test_Runner::TToken{"operator", {91, 99, Token_Type::KEYWORD}});
    CHECK(tokens[34] == Test_Runner::TToken{"<", {99, 100, Token_Type::PUNCTUATION}});
    CHECK(tokens[35] == Test_Runner::TToken{"(", {100, 101, Token_Type::OPEN_PAIR}});
    CHECK(tokens[36] == Test_Runner::TToken{")", {101, 102, Token_Type::CLOSE_PAIR}});
    CHECK(tokens[37] == Test_Runner::TToken{";", {102, 103, Token_Type::PUNCTUATION}});
    CHECK(tokens[38] == Test_Runner::TToken{"bool", {104, 108, Token_Type::TYPE}});
    CHECK(tokens[39] == Test_Runner::TToken{"operator", {109, 117, Token_Type::KEYWORD}});
    CHECK(tokens[40] == Test_Runner::TToken{">", {117, 118, Token_Type::PUNCTUATION}});
    CHECK(tokens[41] == Test_Runner::TToken{"(", {118, 119, Token_Type::OPEN_PAIR}});
    CHECK(tokens[42] == Test_Runner::TToken{")", {119, 120, Token_Type::CLOSE_PAIR}});
    CHECK(tokens[43] == Test_Runner::TToken{";", {120, 121, Token_Type::PUNCTUATION}});
}
