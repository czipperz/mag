#include <cz/buffer_array.hpp>
#include "prose/compiler.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("parse_errors basic") {
    Test_Runner tr;
    tr.setup(R"(command
src/custom/config-2.cpp:1190:39: error: message
 1190 |             buffer->mode.next_token = yntax::build_next_token;
      |                                       ^~~~~
)");

    cz::Buffer_Array buffer_array;
    buffer_array.init();
    CZ_DEFER(buffer_array.drop());

    WITH_CONST_SELECTED_BUFFER(&tr.client);
    prose::All_Messages all_messages = prose::parse_errors(buffer->contents.start(), buffer_array.allocator());
    REQUIRE(all_messages.file_names.len == 1);
    CHECK(all_messages.file_names[0] == "src/custom/config-2.cpp");

    REQUIRE(all_messages.file_messages.len == 1);
    REQUIRE(all_messages.file_messages[0].lines.len == 1);
    REQUIRE(all_messages.file_messages[0].columns.len == 1);
    REQUIRE(all_messages.file_messages[0].messages.len == 1);
    CHECK(all_messages.file_messages[0].lines[0] == 1190);
    CHECK(all_messages.file_messages[0].columns[0] == 39);
    CHECK(all_messages.file_messages[0].messages[0] == "error: message");
}
