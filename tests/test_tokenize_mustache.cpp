#include "syntax/tokenize_mustache.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("tokenize_mustache") {
    Test_Runner tr;
    tr.setup(
        "{{!normal.mustache}}\n"
        "{{$text}}Here goes nothing.{{/text}}\n"
        "\n"
        "{{!bold.mustache}}\n"
        "<b>{{$text}}Here also goes nothing but it's bold.{{/text}}</b>\n"
        "\n"
        "{{!dynamic.mustache}}\n"
        "{{<*dynamic}}\n"
        "{{$text}}Hello World!{{/text}}\n"
        "{{/*dynamic}}\n");
    tr.set_tokenizer(syntax::mustache_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 35);
    CHECK(tokens[0] == Test_Runner::TToken{"{{!normal.mustache}}", {0, 20, Token_Type::COMMENT}});
    CHECK(tokens[1] == Test_Runner::TToken{"{{$text}}", {21, 30, Token_Type::PREPROCESSOR_IF}});
    CHECK(tokens[2] == Test_Runner::TToken{"Here", {30, 34, Token_Type::IDENTIFIER}});
    CHECK(tokens[3] == Test_Runner::TToken{"goes", {35, 39, Token_Type::IDENTIFIER}});
    CHECK(tokens[4] == Test_Runner::TToken{"nothing", {40, 47, Token_Type::IDENTIFIER}});
    CHECK(tokens[5] == Test_Runner::TToken{".", {47, 48, Token_Type::PUNCTUATION}});
    CHECK(tokens[6] == Test_Runner::TToken{"{{/text}}", {48, 57, Token_Type::PREPROCESSOR_ENDIF}});
    CHECK(tokens[7] == Test_Runner::TToken{"{{!bold.mustache}}", {59, 77, Token_Type::COMMENT}});
    CHECK(tokens[8] == Test_Runner::TToken{"<", {78, 79, Token_Type::PUNCTUATION}});
    CHECK(tokens[9] == Test_Runner::TToken{"b", {79, 80, Token_Type::IDENTIFIER}});
    CHECK(tokens[10] == Test_Runner::TToken{">", {80, 81, Token_Type::PUNCTUATION}});
    CHECK(tokens[11] == Test_Runner::TToken{"{{$text}}", {81, 90, Token_Type::PREPROCESSOR_IF}});
    CHECK(tokens[12] == Test_Runner::TToken{"Here", {90, 94, Token_Type::IDENTIFIER}});
    CHECK(tokens[13] == Test_Runner::TToken{"also", {95, 99, Token_Type::IDENTIFIER}});
    CHECK(tokens[14] == Test_Runner::TToken{"goes", {100, 104, Token_Type::IDENTIFIER}});
    CHECK(tokens[15] == Test_Runner::TToken{"nothing", {105, 112, Token_Type::IDENTIFIER}});
    CHECK(tokens[16] == Test_Runner::TToken{"but", {113, 116, Token_Type::IDENTIFIER}});
    CHECK(tokens[17] == Test_Runner::TToken{"it", {117, 119, Token_Type::IDENTIFIER}});
    CHECK(tokens[18] == Test_Runner::TToken{"'", {119, 120, Token_Type::DEFAULT}});
    CHECK(tokens[19] == Test_Runner::TToken{"s", {120, 121, Token_Type::IDENTIFIER}});
    CHECK(tokens[20] == Test_Runner::TToken{"bold", {122, 126, Token_Type::IDENTIFIER}});
    CHECK(tokens[21] == Test_Runner::TToken{".", {126, 127, Token_Type::PUNCTUATION}});
    CHECK(tokens[22] == Test_Runner::TToken{"{{/text}}", {127, 136, Token_Type::PREPROCESSOR_ENDIF}});
    CHECK(tokens[23] == Test_Runner::TToken{"<", {136, 137, Token_Type::PUNCTUATION}});
    CHECK(tokens[24] == Test_Runner::TToken{"/", {137, 138, Token_Type::PUNCTUATION}});
    CHECK(tokens[25] == Test_Runner::TToken{"b", {138, 139, Token_Type::IDENTIFIER}});
    CHECK(tokens[26] == Test_Runner::TToken{">", {139, 140, Token_Type::PUNCTUATION}});
    CHECK(tokens[27] == Test_Runner::TToken{"{{!dynamic.mustache}}", {142, 163, Token_Type::COMMENT}});
    CHECK(tokens[28] == Test_Runner::TToken{"{{<*dynamic}}", {164, 177, Token_Type::PREPROCESSOR_IF}});
    CHECK(tokens[29] == Test_Runner::TToken{"{{$text}}", {178, 187, Token_Type::PREPROCESSOR_IF}});
    CHECK(tokens[30] == Test_Runner::TToken{"Hello", {187, 192, Token_Type::IDENTIFIER}});
    CHECK(tokens[31] == Test_Runner::TToken{"World", {193, 198, Token_Type::IDENTIFIER}});
    CHECK(tokens[32] == Test_Runner::TToken{"!", {198, 199, Token_Type::PUNCTUATION}});
    CHECK(tokens[33] == Test_Runner::TToken{"{{/text}}", {199, 208, Token_Type::PREPROCESSOR_ENDIF}});
    CHECK(tokens[34] == Test_Runner::TToken{"{{/*dynamic}}", {209, 222, Token_Type::PREPROCESSOR_ENDIF}});
}
