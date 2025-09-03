#include <czt/test_base.hpp>

#include "basic/build_commands.hpp"
#include "syntax/tokenize_build.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("file_navigation") {
    Test_Runner tr;
    tr.set_tokenizer(syntax::build_next_token);

    cz::Heap_String body = {};
    CZ_DEFER(body.drop());
    const size_t count = 100;
    for (size_t i = 0; i < count; ++i) {
        cz::append(&body, "[", i, "] a bunch of text on the same line blah blah blah\n");
    }
    tr.setup(body);

    {
        WITH_SELECTED_BUFFER(&tr.client);
        buffer->token_cache.update(buffer);
        buffer->token_cache.generate_check_points_until(buffer, buffer->contents.len);
        window->cursors[window->selected_cursor].point = buffer->contents.len;
    }

    for (size_t i = 0; i < count; ++i) {
        tr.run(basic::command_build_previous_file);
    }

    WITH_CONST_SELECTED_BUFFER(&tr.client);
    CHECK(window->cursors[window->selected_cursor].point == 0);
}
