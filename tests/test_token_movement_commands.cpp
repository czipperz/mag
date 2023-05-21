#include <czt/test_base.hpp>

#include "basic/token_movement_commands.hpp"
#include "test_runner.hpp"

using namespace cz;
using namespace mag;
using namespace mag::basic;

TEST_CASE("command_backward_up_token_pair_or_indent") {
    cz::Str body =
        "\n\
|class| Class:\n\
  |def f():\n\
    |pa|ss\n\
\n\
  def| g(|):\n\
|\n\
    sup|\n\
\n\
\n\
|def h():|";

    cz::Vector<Cursor> cursors = {};
    CZ_DEFER(cursors.drop(cz::heap_allocator()));
    {
        Test_Runner tr;
        tr.setup(body);
        cursors = tr.client.selected_window()->cursors.clone(cz::heap_allocator());
    }

    for (size_t i = 0; i < cursors.len; ++i) {
        Test_Runner tr;
        tr.setup(body);
        Window_Unified* window = tr.client.selected_window();
        window->selected_cursor = 0;
        window->cursors.len = 0;
        window->cursors.push(cursors[i]);

        tr.run(command_backward_up_token_pair_or_indent);

        uint64_t dest = window->cursors[0].point;
        INFO("i: " << i);
        INFO("cursors[i]: " << cursors[i].point);
        switch (i) {
        case 0:  // |class Class:
        case 2:  //   |def f():
        case 5:  //   def| g():
            CHECK(dest == cursors[0].point);
            break;
        case 1:  // class| Class:
            CHECK(dest == cursors[1].point);
            break;
        case 3:  //     |pass
        case 4:  //     pa|ss
            CHECK(dest == cursors[2].point);
            break;
        case 6:  //   def g(|):
            CHECK(dest == cursors[6].point - 1);
            break;
        case 7:  // |
        case 8:  //     sup|
            CHECK(dest == cursors[5].point - 3);
            break;
        case 9:  // |def h():
            CHECK(dest == cursors[9].point);
            break;
        case 10:  // def h():|
            CHECK(dest == cursors[10].point);
            break;

        default:
            FAIL("unreachable");
        }
    }
}
