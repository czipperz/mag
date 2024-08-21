#include "test_runner.hpp"

#include "basic/window_commands.hpp"

using namespace mag;
using namespace mag::basic;

TEST_CASE("shift_window rotate") {
    Test_Runner tr;
    tr.open_temp_file("a", {});
    tr.run(command_split_window_horizontal);
    tr.open_temp_file("b", {});
    tr.run(command_split_window_vertical);
    tr.open_temp_file("c", {});

    CHECK(tr.stringify_client_layout() ==
          "Horizontal:\n"
          "  *a*\n"
          "  Vertical:\n"
          "    *b*\n"
          "    *c*\n");

    //      a
    // -----------
    //   b  | (c)

    tr.run(command_shift_window_right);

    //   a  |
    // -----| (c)
    //   b  |

    CHECK(tr.stringify_client_layout() ==
          "Vertical:\n"
          "  Horizontal:\n"
          "    *a*\n"
          "    *b*\n"
          "  *c*\n");
}
