#include "commands.hpp"

#include <ctype.h>
#include <cz/defer.hpp>
#include <cz/option.hpp>
#include <cz/util.hpp>
#include <stdio.h>
#include "command_macros.hpp"
#include "file.hpp"
#include "insert.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "visible_region_commands.hpp"

namespace mag {
namespace basic {

void command_insert_numbers(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(cursors.len, 0);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        char buffer[SSOStr::MAX_SHORT_LEN];
        int len = snprintf(buffer, sizeof(buffer), "%d", (int)i);
        CZ_DEBUG_ASSERT(len > 0);

        Edit insert;
        insert.value =
            SSOStr::from_constant({buffer, std::min((size_t)len, SSOStr::MAX_SHORT_LEN)});
        insert.position = cursors[i].point + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += len;
    }

    transaction.commit(buffer);
}

static void change_numbers(Editor* editor, Command_Source source, int difference) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(cursors.len * 2, 0);
    CZ_DEFER(transaction.drop());

    int64_t offset = 0;
    Contents_Iterator iterator = buffer->contents.start();
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        if (!iterator.at_bob() && !isdigit(iterator.get())) {
            iterator.retreat();
        }
        while (!iterator.at_bob()) {
            char ch = iterator.get();
            if (ch != '-' && !isdigit(ch)) {
                iterator.advance();
                break;
            }
            iterator.retreat();
        }

        char buf[SSOStr::MAX_SHORT_LEN + 1];
        size_t j = 0;
        for (; j + 1 < sizeof(buf); ++j) {
            if (iterator.at_eob()) {
                break;
            }
            char ch = iterator.get();
            if (ch != '-' && !isdigit(ch)) {
                break;
            }
            buf[j] = ch;
            iterator.advance();
        }
        buf[j] = '\0';

        int val;
        int scan_ret = sscanf(buf, "%d", &val);
        if (scan_ret != 1) {
            // Invalid input.
            continue;
        }

        Edit remove;
        iterator.retreat(j);
        remove.value = SSOStr::from_constant({buf, j});
        remove.position = iterator.position + offset;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        int len = snprintf(buf, sizeof(buf), "%d", val + difference);
        CZ_DEBUG_ASSERT(len > 0);

        Edit insert;
        insert.value = SSOStr::from_constant({buf, std::min((size_t)len, SSOStr::MAX_SHORT_LEN)});
        insert.position = iterator.position + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += len;
        offset -= j;
    }

    transaction.commit(buffer);
}

void command_increment_numbers(Editor* editor, Command_Source source) {
    change_numbers(editor, source, +1);
}

void command_decrement_numbers(Editor* editor, Command_Source source) {
    change_numbers(editor, source, -1);
}

}
}
