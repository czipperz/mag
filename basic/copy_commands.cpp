#include "copy_commands.hpp"

#include <inttypes.h>
#include <stdio.h>
#include "command_macros.hpp"

namespace mag {
namespace basic {

static void save_copy(Copy_Chain** cursor_chain, Editor* editor, SSOStr value, Client* client) {
    Copy_Chain* chain = editor->copy_buffer.allocator().alloc<Copy_Chain>();
    chain->value = value;
    chain->previous = *cursor_chain;
    *cursor_chain = chain;

    if (client) {
        client->set_system_clipboard(value.as_str());
    }
}

static void cut_cursor(Cursor* cursor,
                       Transaction* transaction,
                       Copy_Chain** copy_chain,
                       Editor* editor,
                       Buffer* buffer,
                       uint64_t* offset,
                       Client* client) {
    uint64_t start = cursor->start();
    uint64_t end = cursor->end();

    Edit edit;
    edit.value = buffer->contents.slice(editor->copy_buffer.allocator(),
                                        buffer->contents.iterator_at(start), end);
    edit.position = start - *offset;
    *offset += end - start;
    edit.flags = Edit::REMOVE;
    transaction->push(edit);

    save_copy(copy_chain, editor, edit.value, client);
}

REGISTER_COMMAND(command_cut);
void command_cut(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    uint64_t offset = 0;
    if (cursors.len == 1) {
        cut_cursor(&cursors[0], &transaction, &source.client->global_copy_chain, editor, buffer,
                   &offset, source.client);
    } else {
        for (size_t c = 0; c < cursors.len; ++c) {
            cut_cursor(&cursors[c], &transaction, &cursors[c].local_copy_chain, editor, buffer,
                       &offset, nullptr);
        }
    }

    transaction.commit(source.client);

    window->show_marks = false;
}

static void copy_cursor(Cursor* cursor,
                        Copy_Chain** copy_chain,
                        Editor* editor,
                        const Buffer* buffer,
                        Client* client) {
    uint64_t start = cursor->start();
    uint64_t end = cursor->end();
    // :CopyLeak We allocate here.
    save_copy(copy_chain, editor,
              buffer->contents.slice(editor->copy_buffer.allocator(),
                                     buffer->contents.iterator_at(start), end),
              client);
}

REGISTER_COMMAND(command_copy);
void command_copy(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    source.client->update_global_copy_chain(editor);
    cz::Slice<Cursor> cursors = window->cursors;
    if (cursors.len == 1) {
        copy_cursor(&cursors[0], &source.client->global_copy_chain, editor, buffer, source.client);
    } else {
        for (size_t c = 0; c < cursors.len; ++c) {
            copy_cursor(&cursors[c], &cursors[c].local_copy_chain, editor, buffer, nullptr);
        }
    }

    window->show_marks = false;
}

static bool setup_paste(cz::Slice<Cursor> cursors, Copy_Chain* global_copy_chain) {
    for (size_t c = 0; c < cursors.len; ++c) {
        cursors[c].paste_local = cursors[c].local_copy_chain;
        cursors[c].paste_global = global_copy_chain;
        if (!cursors[c].paste_local && !cursors[c].paste_global) {
            return false;
        }
    }
    return true;
}

static void run_paste(Client* client, Window_Unified* window, Buffer* buffer) {
    // :CopyLeak Probably we will need to copy all the values here.
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;

    cz::Vector<uint64_t> befores = {};
    CZ_DEFER(befores.drop(cz::heap_allocator()));
    befores.reserve(cz::heap_allocator(), cursors.len);

    uint64_t offset = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        Copy_Chain* copy_chain = cursors[c].paste_local;
        if (!copy_chain) {
            copy_chain = cursors[c].paste_global;
        }

        if (copy_chain) {
            Edit edit;
            // :CopyLeak We sometimes use the value here, but we could also
            // just copy a bunch of stuff then close the cursors and leak
            // that memory.
            edit.value = copy_chain->value;
            edit.position = cursors[c].point + offset;
            offset += edit.value.len();
            edit.flags = Edit::INSERT;
            transaction.push(edit);

            befores.push(edit.position);
        } else {
            befores.push(cursors[c].point + offset);
        }
    }

    transaction.commit(client);

    // Put mark at the start of the paste region.
    if (!window->show_marks) {
        window->update_cursors(buffer);
        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].mark = befores[c];
        }
    }
}

REGISTER_COMMAND(command_paste);
void command_paste(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    source.client->update_global_copy_chain(editor);
    if (!setup_paste(window->cursors, source.client->global_copy_chain)) {
        return;
    }

    run_paste(source.client, window, buffer);
}

REGISTER_COMMAND(command_paste_previous);
void command_paste_previous(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    if (source.previous_command.function == command_paste) {
        if (!setup_paste(window->cursors, source.client->global_copy_chain)) {
            return;
        }
    }

    if (source.previous_command.function == command_paste ||
        source.previous_command.function == command_paste_previous) {
        for (size_t c = 0; c < cursors.len; ++c) {
            Copy_Chain** chain = &cursors[c].paste_local;
            if (*chain) {
                *chain = (*chain)->previous;
                if (!*chain) {
                    chain = &cursors[c].paste_global;
                }
            } else {
                chain = &cursors[c].paste_global;
                if (*chain) {
                    *chain = (*chain)->previous;
                }
                if (!*chain) {
                    return;
                }
            }
        }

        {
            WITH_WINDOW_BUFFER(window);
            buffer->undo();
            window->update_cursors(buffer);
            run_paste(source.client, window, buffer);
        }
    } else {
        source.client->show_message("Error: previous command was not paste");
    }
}

static void copy_length_cursor(Cursor* cursor,
                               Copy_Chain** copy_chain,
                               Editor* editor,
                               Client* client) {
    uint64_t start = cursor->start();
    uint64_t end = cursor->end();

    size_t len = snprintf(nullptr, 0, "%" PRIu64, end - start);
    char* buffer = editor->copy_buffer.allocator().alloc<char>(len + 1);
    CZ_ASSERT(buffer);
    snprintf(buffer, len + 1, "%" PRIu64, end - start);

    // :CopyLeak We allocate here.
    save_copy(copy_chain, editor, SSOStr::from_constant({buffer, len}), client);
}

REGISTER_COMMAND(command_copy_selected_region_length);
void command_copy_selected_region_length(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    if (cursors.len == 1) {
        copy_length_cursor(&cursors[0], &source.client->global_copy_chain, editor, source.client);
    } else {
        for (size_t c = 0; c < cursors.len; ++c) {
            copy_length_cursor(&cursors[c], &cursors[c].local_copy_chain, editor, nullptr);
        }
    }

    window->show_marks = false;
}

static void cut_cursor_as_lines(Cursor* cursor,
                                Transaction* transaction,
                                Copy_Chain** copy_chain,
                                Editor* editor,
                                uint64_t* offset,
                                cz::Str string,
                                uint64_t* string_offset,
                                Contents_Iterator* iterator,
                                Client* client) {
    uint64_t start = cursor->start();
    uint64_t end = cursor->end();

    Edit edit;
    edit.value = SSOStr::from_constant(
        string.slice(*offset + *string_offset, *offset + *string_offset + end - start));
    edit.position = start - *offset;
    *offset += end - start;
    edit.flags = Edit::REMOVE;
    transaction->push(edit);

    if (end > 0) {
        iterator->advance_to(end - 1);
        if (iterator->get() == '\n') {
            --*string_offset;
        }
    }

    save_copy(copy_chain, editor, SSOStr::from_constant(string), client);
}

static cz::String copy_cursors_as_lines(Editor* editor,
                                        const Buffer* buffer,
                                        cz::Slice<Cursor> cursors) {
    uint64_t sum_region_sizes = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        sum_region_sizes += cursors[c].end() - cursors[c].start();
    }

    cz::String string = {};
    string.reserve(editor->copy_buffer.allocator(), sum_region_sizes + cursors.len);
    Contents_Iterator iterator = buffer->contents.iterator_at(cursors[0].start());
    for (size_t c = 0; c < cursors.len; ++c) {
        iterator.advance_to(cursors[c].start());
        buffer->contents.slice_into(iterator, cursors[c].end(), &string);
        if (!string.ends_with("\n")) {
            string.push('\n');
        }
    }
    string.realloc(editor->copy_buffer.allocator());
    return string;
}

REGISTER_COMMAND(command_cursors_cut_as_lines);
void command_cursors_cut_as_lines(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<Cursor> cursors = window->cursors;
    cz::String string = copy_cursors_as_lines(editor, buffer, cursors);

    uint64_t offset = 0;
    uint64_t string_offset = 0;
    Contents_Iterator iterator = buffer->contents.start();
    if (cursors.len == 1) {
        cut_cursor_as_lines(&cursors[0], &transaction, &source.client->global_copy_chain, editor,
                            &offset, string, &string_offset, &iterator, source.client);
    } else {
        for (size_t c = 0; c < cursors.len; ++c) {
            cut_cursor_as_lines(&cursors[c], &transaction, &cursors[c].local_copy_chain, editor,
                                &offset, string, &string_offset, &iterator, nullptr);
            ++string_offset;
        }
    }

    transaction.commit(source.client);

    window->show_marks = false;
}

REGISTER_COMMAND(command_cursors_copy_as_lines);
void command_cursors_copy_as_lines(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    source.client->update_global_copy_chain(editor);
    cz::Slice<Cursor> cursors = window->cursors;

    cz::String string = copy_cursors_as_lines(editor, buffer, cursors);

    if (cursors.len == 1) {
        save_copy(&source.client->global_copy_chain, editor, SSOStr::from_constant(string),
                  source.client);
    } else {
        for (size_t c = 0; c < cursors.len; ++c) {
            save_copy(&cursors[c].local_copy_chain, editor, SSOStr::from_constant(string),
                      source.client);
        }
    }

    window->show_marks = false;
}

static void run_paste_as_lines(Client* client, cz::Slice<Cursor> cursors, Buffer* buffer) {
    // :CopyLeak Probably we will need to copy all the values here.
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Copy_Chain* copy_chain = cursors[0].paste_local;
    if (!copy_chain) {
        copy_chain = cursors[0].paste_global;
    }

    if (!copy_chain) {
        return;
    }

    cz::Str value = copy_chain->value.as_str();

    size_t offset = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        cz::Str line = value;
        bool next_line = line.split_excluding('\n', &line, &value);

        Edit edit;
        // :CopyLeak We sometimes use the value here, but we could also
        // just copy a bunch of stuff then close the cursors and leak
        // that memory.
        edit.value = SSOStr::from_constant(line);
        edit.position = cursors[c].point + offset;
        offset += edit.value.len();
        edit.flags = Edit::INSERT;
        transaction.push(edit);

        if (!next_line) {
            break;
        }
    }

    transaction.commit(client);
}

REGISTER_COMMAND(command_cursors_paste_as_lines);
void command_cursors_paste_as_lines(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    source.client->update_global_copy_chain(editor);
    cz::Slice<Cursor> cursors = window->cursors;
    if (!setup_paste(window->cursors, source.client->global_copy_chain)) {
        return;
    }

    run_paste_as_lines(source.client, cursors, buffer);
}

REGISTER_COMMAND(command_cursors_paste_previous_as_lines);
void command_cursors_paste_previous_as_lines(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    if (source.previous_command.function == command_cursors_paste_as_lines) {
        if (!setup_paste(window->cursors, source.client->global_copy_chain)) {
            return;
        }
    }

    if (source.previous_command.function == command_cursors_paste_as_lines ||
        source.previous_command.function == command_cursors_paste_previous_as_lines) {
        for (size_t c = 0; c < cursors.len; ++c) {
            Copy_Chain** chain = &cursors[c].paste_local;
            if (*chain) {
                *chain = (*chain)->previous;
                if (!*chain) {
                    chain = &cursors[c].paste_global;
                }
            } else {
                chain = &cursors[c].paste_global;
                if (*chain) {
                    *chain = (*chain)->previous;
                }
                if (!*chain) {
                    return;
                }
            }
        }

        {
            WITH_WINDOW_BUFFER(window);
            buffer->undo();
            window->update_cursors(buffer);
            run_paste_as_lines(source.client, cursors, buffer);
        }
    } else {
        source.client->show_message("Error: previous command was not paste");
    }
}

}
}
