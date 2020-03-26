#include "capitalization_commands.hpp"

#include <ctype.h>
#include "command_macros.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void command_uppercase_letter(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(cursors.len * 2, 0);

    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t point = cursors[c].point;
        char ch = buffer->contents.get_once(point);

        Edit remove;
        remove.value.init_char(ch);
        remove.position = point;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value.init_char(toupper(ch));
        insert.position = point;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(buffer);
}

void command_lowercase_letter(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(cursors.len * 2, 0);

    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t point = cursors[c].point;
        char ch = buffer->contents.get_once(point);

        Edit remove;
        remove.value.init_char(ch);
        remove.position = point;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value.init_char(tolower(ch));
        insert.position = point;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(buffer);
}

void command_uppercase_region(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->show_marks) {
        return;
    }

    WITH_WINDOW_BUFFER(window);

    cz::Slice<Cursor> cursors = window->cursors;
    uint64_t sum_region_sizes = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        sum_region_sizes += cursors[c].end() - cursors[c].start();
    }

    Transaction transaction;
    transaction.init(window->cursors.len() * 2, sum_region_sizes * 2);

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
        uint64_t end = cursors[c].end();

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end);
        remove.position = start.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value.init_duplicate(transaction.value_allocator(), remove.value.as_str());
        cz::Str str = insert.value.as_str();
        for (size_t i = 0; i < str.len; ++i) {
            ((char*)str.buffer)[i] = toupper(str.buffer[i]);
        }

        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(buffer);

    window->show_marks = false;
}

void command_lowercase_region(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->show_marks) {
        return;
    }

    WITH_WINDOW_BUFFER(window);

    cz::Slice<Cursor> cursors = window->cursors;
    uint64_t sum_region_sizes = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        sum_region_sizes += cursors[c].end() - cursors[c].start();
    }

    Transaction transaction;
    transaction.init(window->cursors.len() * 2, sum_region_sizes * 2);

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
        uint64_t end = cursors[c].end();

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end);
        remove.position = start.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value.init_duplicate(transaction.value_allocator(), remove.value.as_str());
        cz::Str str = insert.value.as_str();
        for (size_t i = 0; i < str.len; ++i) {
            ((char*)str.buffer)[i] = tolower(str.buffer[i]);
        }

        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(buffer);

    window->show_marks = false;
}

}
}
