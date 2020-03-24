#include "copy_commands.hpp"

#include "command_macros.hpp"

namespace mag {
namespace custom {

static void save_copy(Copy_Chain** cursor_chain, Editor* editor, SSOStr value) {
    Copy_Chain* chain = editor->copy_buffer.allocator().alloc<Copy_Chain>();
    chain->value = value;
    chain->previous = *cursor_chain;
    *cursor_chain = chain;
}

static void cut_cursor(Cursor* cursor,
                       Transaction* transaction,
                       Copy_Chain** copy_chain,
                       Editor* editor,
                       Buffer* buffer,
                       uint64_t* offset) {
    uint64_t start = cursor->start();
    uint64_t end = cursor->end();

    Edit edit;
    edit.value = buffer->contents.slice(transaction->value_allocator(),
                                        buffer->contents.iterator_at(start), end);
    edit.position = start - *offset;
    *offset += end - start;
    edit.is_insert = false;
    transaction->push(edit);

    save_copy(copy_chain, editor, edit.value);
}

void command_cut(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER();
    WITH_TRANSACTION({
        cz::Slice<Cursor> cursors = window->cursors;

        uint64_t sum_region_sizes = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            sum_region_sizes += cursors[c].end() - cursors[c].start();
        }
        transaction.init(cursors.len, sum_region_sizes);

        size_t offset = 0;
        if (cursors.len == 1) {
            cut_cursor(&cursors[0], &transaction, &source.client->global_copy_chain, editor, buffer,
                       &offset);
        } else {
            for (size_t c = 0; c < cursors.len; ++c) {
                cut_cursor(&cursors[c], &transaction, &cursors[c].local_copy_chain, editor, buffer,
                           &offset);
            }
        }

        window->show_marks = false;
    });
}

static void copy_cursor(Cursor* cursor, Copy_Chain** copy_chain, Editor* editor, Buffer* buffer) {
    uint64_t start = cursor->start();
    uint64_t end = cursor->end();
    // :CopyLeak We allocate here.
    save_copy(copy_chain, editor,
              buffer->contents.slice(editor->copy_buffer.allocator(),
                                     buffer->contents.iterator_at(start), end));
}

void command_copy(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER();
    cz::Slice<Cursor> cursors = window->cursors;
    if (cursors.len == 1) {
        copy_cursor(&cursors[0], &source.client->global_copy_chain, editor, buffer);
    } else {
        for (size_t c = 0; c < cursors.len; ++c) {
            copy_cursor(&cursors[c], &cursors[c].local_copy_chain, editor, buffer);
        }
    }

    window->show_marks = false;
}

void command_paste(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER();
    WITH_TRANSACTION({
        cz::Slice<Cursor> cursors = window->cursors;
        // :CopyLeak Probably we will need to copy all the values herea.
        transaction.init(cursors.len, 0);

        size_t offset = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            Copy_Chain* copy_chain = nullptr;
            if (!copy_chain) {
                copy_chain = cursors[c].local_copy_chain;
            }
            if (!copy_chain) {
                copy_chain = source.client->global_copy_chain;
            }

            if (copy_chain) {
                Edit edit;
                // :CopyLeak We sometimes use the value here, but we could also
                // just copy a bunch of stuff then close the cursors and leak
                // that memory.
                edit.value = copy_chain->value;
                edit.position = cursors[c].point + offset;
                offset += edit.value.len();
                edit.is_insert = true;
                transaction.push(edit);
            }
        }
    });
}

}
}
