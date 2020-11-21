#include "insert.hpp"

#include <cz/defer.hpp>
#include "buffer.hpp"
#include "ssostr.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {

void insert(Buffer* buffer, Window_Unified* window, SSOStr value, Command_Function committer) {
    window->update_cursors(buffer);

    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(cursors.len, 0);
    CZ_DEFER(transaction.drop());

    for (size_t i = 0; i < cursors.len; ++i) {
        Edit edit;
        edit.value = value;
        edit.position = cursors[i].point + i;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    transaction.commit(buffer, committer);
}

void insert_char(Buffer* buffer, Window_Unified* window, char code, Command_Function committer) {
    insert(buffer, window, SSOStr::from_char(code), committer);
}

}
