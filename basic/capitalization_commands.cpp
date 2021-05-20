#include "capitalization_commands.hpp"

#include <cz/char_type.hpp>
#include "command_macros.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void command_uppercase_letter(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t point = cursors[c].point;
        char ch = buffer->contents.get_once(point);

        Edit remove;
        remove.value = SSOStr::from_char(ch);
        remove.position = point;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = SSOStr::from_char(cz::to_upper(ch));
        insert.position = point;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();
}

void command_lowercase_letter(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t point = cursors[c].point;
        char ch = buffer->contents.get_once(point);

        Edit remove;
        remove.value = SSOStr::from_char(ch);
        remove.position = point;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = SSOStr::from_char(cz::to_lower(ch));
        insert.position = point;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();
}

void command_uppercase_region(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->show_marks) {
        return;
    }

    WITH_WINDOW_BUFFER(window);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
        uint64_t end = cursors[c].end();

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end);
        remove.position = start.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = remove.value.duplicate(transaction.value_allocator());
        cz::Str str = insert.value.as_str();
        for (size_t i = 0; i < str.len; ++i) {
            ((char*)str.buffer)[i] = cz::to_upper(str.buffer[i]);
        }

        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();

    window->show_marks = false;
}

void command_lowercase_region(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    if (!window->show_marks) {
        return;
    }

    WITH_WINDOW_BUFFER(window);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].start());
        uint64_t end = cursors[c].end();

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end);
        remove.position = start.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = remove.value.duplicate(transaction.value_allocator());
        cz::Str str = insert.value.as_str();
        for (size_t i = 0; i < str.len; ++i) {
            ((char*)str.buffer)[i] = cz::to_lower(str.buffer[i]);
        }

        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit();

    window->show_marks = false;
}

}
}
