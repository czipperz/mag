#include "basic/table_commands.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("realign_table") {
    Test_Runner test_runner;
    test_runner.append(
        "|a|123|\n"
        "|-\n"
        "|bc|45|\n"
        "|def|6|\n");

    test_runner.run(basic::command_realign_table);

    WITH_CONST_SELECTED_BUFFER(&test_runner.client);
    CHECK(buffer->contents.stringify(test_runner.buffer_array.allocator()) ==
          "|a   |123 |\n"
          "|----|----|\n"
          "|bc  |45  |\n"
          "|def |6   |\n");
}
