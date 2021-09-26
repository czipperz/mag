#include "number_commands.hpp"

#include <stdio.h>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/option.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "insert.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "visible_region_commands.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_insert_numbers);
void command_insert_numbers(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
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

    transaction.commit(source.client);
}

template <class Func>
static void change_numbers(Client* client, Buffer* buffer, Window_Unified* window, Func&& func) {
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    int64_t offset = 0;
    Contents_Iterator iterator = buffer->contents.start();
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        if (!iterator.at_bob() && !cz::is_digit(iterator.get())) {
            iterator.retreat();
        }
        while (!iterator.at_bob()) {
            char ch = iterator.get();
            if (ch != '-' && !cz::is_digit(ch)) {
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
            if (ch != '-' && !cz::is_digit(ch)) {
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

        int len = snprintf(buf, sizeof(buf), "%d", func(val));
        CZ_DEBUG_ASSERT(len > 0);

        Edit insert;
        insert.value = SSOStr::from_constant({buf, std::min((size_t)len, SSOStr::MAX_SHORT_LEN)});
        insert.position = iterator.position + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += len;
        offset -= j;
    }

    transaction.commit(client);
}

REGISTER_COMMAND(command_increment_numbers);
void command_increment_numbers(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    change_numbers(source.client, buffer, window, [](int x) { return x + 1; });
}

REGISTER_COMMAND(command_decrement_numbers);
void command_decrement_numbers(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    change_numbers(source.client, buffer, window, [](int x) { return x - 1; });
}

static void command_prompt_increase_numbers_callback(Editor* editor,
                                                     Client* client,
                                                     cz::Str mini_buffer_contents,
                                                     void* data) {
    char buf[32];
    if (mini_buffer_contents.len > sizeof(buf) - 1) {
    parse_error:
        client->show_message("Error: couldn't parse number to increase by");
        return;
    }

    memcpy(buf, mini_buffer_contents.buffer, mini_buffer_contents.len);
    buf[mini_buffer_contents.len] = '\0';

    int num = 0;
    if (sscanf(buf, "%d", &num) != 1) {
        goto parse_error;
    }

    WITH_SELECTED_BUFFER(client);
    change_numbers(client, buffer, window, [&](int x) { return x + num; });
}

REGISTER_COMMAND(command_prompt_increase_numbers);
void command_prompt_increase_numbers(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Increase numbers by: ";
    dialog.response_callback = command_prompt_increase_numbers_callback;
    source.client->show_dialog(dialog);
}

static void command_prompt_multiply_numbers_callback(Editor* editor,
                                                     Client* client,
                                                     cz::Str mini_buffer_contents,
                                                     void* data) {
    char buf[32];
    if (mini_buffer_contents.len > sizeof(buf) - 1) {
    parse_error:
        client->show_message("Error: couldn't parse number to increase by");
        return;
    }

    memcpy(buf, mini_buffer_contents.buffer, mini_buffer_contents.len);
    buf[mini_buffer_contents.len] = '\0';

    int num = 0;
    if (sscanf(buf, "%d", &num) != 1) {
        goto parse_error;
    }

    WITH_SELECTED_BUFFER(client);
    change_numbers(client, buffer, window, [&](int x) { return x * num; });
}

REGISTER_COMMAND(command_prompt_multiply_numbers);
void command_prompt_multiply_numbers(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Multiply numbers by: ";
    dialog.response_callback = command_prompt_multiply_numbers_callback;
    source.client->show_dialog(dialog);
}

REGISTER_COMMAND(command_insert_letters);
void command_insert_letters(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    if (cursors.len > 26) {
        source.client->show_message(
            "command_insert_letters only supports up to 26 cursors as of right now");
        return;
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Edit insert;
        insert.value = SSOStr::from_char('a' + i);
        insert.position = cursors[i].point + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += 1;
    }

    transaction.commit(source.client);
}

}
}
