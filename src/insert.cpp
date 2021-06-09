#include "insert.hpp"

#include <cz/defer.hpp>
#include "buffer.hpp"
#include "ssostr.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {

void insert(Buffer* buffer, Window_Unified* window, SSOStr value, Command_Function committer) {
    ZoneScoped;

    window->update_cursors(buffer);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        Edit edit;
        edit.value = value;
        edit.position = cursors[i].point + i;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    transaction.commit(committer);
}

void insert_char(Buffer* buffer, Window_Unified* window, char code, Command_Function committer) {
    insert(buffer, window, SSOStr::from_char(code), committer);
}

void delete_regions(Buffer* buffer, Window_Unified* window, Command_Function committer) {
    ZoneScoped;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t i = 0; i < cursors.len; ++i) {
        uint64_t start = cursors[i].start();
        uint64_t end = cursors[i].end();

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(),
                                            buffer->contents.iterator_at(start), end);
        edit.position = start - offset;
        offset += end - start;
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    transaction.commit(committer);
}

}
