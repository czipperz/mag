#include "syntax/tokenize_python.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("tokenize_python") {
    Test_Runner tr;
    tr.setup(
        "\"\"\" hello world \"\"\"\n"
        "''' hello world '''\n");
    tr.set_tokenizer(syntax::python_next_token);
    // tr.tokenize_print_tests();

    auto tokens = tr.tokenize();
    REQUIRE(tokens.len == 2);
    CHECK(tokens[0] ==
          Test_Runner::TToken{"\"\"\" hello world \"\"\"", {0, 19, Token_Type::STRING}});
    CHECK(tokens[1] == Test_Runner::TToken{"''' hello world '''", {20, 39, Token_Type::STRING}});
}
