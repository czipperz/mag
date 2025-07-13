#include <czt/test_base.hpp>

#include "basic/commands.hpp"
#include "core/insert.hpp"
#include "test_runner.hpp"

using namespace cz;
using namespace mag;
using namespace mag::basic;

TEST_CASE("command_restore_last_save_point") {
    Test_Runner tr;
    {
        WITH_SELECTED_BUFFER(&tr.client);
        insert_char(&tr.client, buffer, window, '1');
        insert_char(&tr.client, buffer, window, '2');
        insert_char(&tr.client, buffer, window, '3');
        insert_char(&tr.client, buffer, window, '4');
        insert_char(&tr.client, buffer, window, '5');
        buffer->mark_saved();
    }
    CHECK(tr.stringify() == "12345|");
    tr.run(command_restore_last_save_point);
    CHECK(tr.stringify() == "12345|");

    printf("\n\n");

    {
        WITH_SELECTED_BUFFER(&tr.client);
        buffer->undo();
        buffer->undo();
        buffer->redo();
        insert_char(&tr.client, buffer, window, '6');
        buffer->undo();
        insert_char(&tr.client, buffer, window, '7');
    }
    CHECK(tr.stringify() == "12347|");
    tr.run(command_restore_last_save_point);
    CHECK(tr.stringify() == "12345|");
}
