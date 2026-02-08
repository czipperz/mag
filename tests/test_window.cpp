#include <czt/test_base.hpp>

#include "core/insert.hpp"
#include "core/window.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("update_cursors basic") {
    Test_Runner tr;
    WITH_SELECTED_BUFFER(&tr.client);

    insert(&tr.client, buffer, window, SSOStr::from_constant("h"));
    window->update_cursors(buffer, &tr.client);
    CHECK(window->cursors[0].mark == 1);
    CHECK(window->cursors[0].point == 1);

    window->cursors[0].mark = 0;
    insert(&tr.client, buffer, window, SSOStr::from_constant("ola"));
    window->update_cursors(buffer, &tr.client);
    CHECK(window->cursors[0].mark == 0);
    CHECK(window->cursors[0].point == 4);

    buffer->undo();
    window->update_cursors(buffer, &tr.client);
    CHECK(window->cursors[0].mark == 0);
    CHECK(window->cursors[0].point == 1);

    buffer->undo();
    window->update_cursors(buffer, &tr.client);
    CHECK(window->cursors[0].mark == 0);
    CHECK(window->cursors[0].point == 0);
}

TEST_CASE("update_cursors replace multiple") {
    Test_Runner tr;
    tr.setup_region("(y|\n(y|\n");
    WITH_SELECTED_BUFFER(&tr.client);
    CHECK(window->cursors[0].mark == 0);
    CHECK(window->cursors[0].point == 1);
    CHECK(window->cursors[1].mark == 2);
    CHECK(window->cursors[1].point == 3);

    delete_regions(&tr.client, buffer, window);
    window->update_cursors(buffer, &tr.client);
    CHECK(window->cursors[0].mark == 0);
    CHECK(window->cursors[0].point == 0);
    CHECK(window->cursors[1].mark == 1);
    CHECK(window->cursors[1].point == 1);

    insert(&tr.client, buffer, window, SSOStr::from_constant("a"));
    window->update_cursors(buffer, &tr.client);
    CHECK(window->cursors[0].mark == 1);
    CHECK(window->cursors[0].point == 1);
    CHECK(window->cursors[1].mark == 3);
    CHECK(window->cursors[1].point == 3);
}

TEST_CASE("update_cursors insert after") {
    Test_Runner tr;
    tr.setup_region("(y|\n(y|\n");
    WITH_SELECTED_BUFFER(&tr.client);

    {
        Transaction transaction;
        transaction.init(buffer);
        CZ_DEFER(transaction.drop());

        cz::Slice<Cursor> cursors = window->cursors;
        uint64_t offset = 0;
        for (size_t i = 0; i < cursors.len; ++i) {
            Edit edit;
            edit.value = SSOStr::from_constant("es");
            edit.position = cursors[i].point + offset;
            edit.flags = Edit::INSERT_AFTER_POSITION;
            transaction.push(edit);
            offset += edit.value.len();
        }

        transaction.commit(&tr.client);
    }

    window->update_cursors(buffer, &tr.client);
    CHECK(window->cursors[0].mark == 0);
    CHECK(window->cursors[0].point == 1);
    CHECK(window->cursors[1].mark == 4);
    CHECK(window->cursors[1].point == 5);
}
