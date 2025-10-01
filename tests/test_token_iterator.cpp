#include "core/token_iterator.hpp"
#include "syntax/tokenize_cplusplus.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("Forward_Token_Iterator init_at_or_after") {
    Test_Runner tr;
    tr.setup("void()");
    tr.set_tokenizer(syntax::cpp_next_token);

    WITH_SELECTED_BUFFER(&tr.client);
    Forward_Token_Iterator it;

    for (uint64_t pos = 0; pos < 4; ++pos) {
        REQUIRE(it.init_at_or_after(buffer, pos));
        CHECK(it.token().start == 0);
        CHECK(it.token().end == 4);
    }

    REQUIRE(it.init_at_or_after(buffer, 4));
    CHECK(it.token().start == 4);
    CHECK(it.token().end == 5);

    REQUIRE(it.init_at_or_after(buffer, 5));
    CHECK(it.token().start == 5);
    CHECK(it.token().end == 6);

    REQUIRE_FALSE(it.init_at_or_after(buffer, 6));
}
