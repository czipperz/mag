#include "diff.hpp"

#include <ctype.h>
#include <stdio.h>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include <cz/try.hpp>
#include "buffer.hpp"
#include "client.hpp"
#include "edit.hpp"
#include "file.hpp"
#include "transaction.hpp"

namespace mag {

struct File_Wrapper {
    cz::Input_File file;
    cz::Carriage_Return_Carry carry;
    char buffer[1024];
    size_t index = 0;
    size_t len = 0;

    int load_more() {
        if (index != len) {
            return 0;
        }

        int64_t l = file.read_text(buffer, sizeof(buffer), &carry);
        if (l < 0) {
            return -1;
        } else if (l == 0) {
            return 1;
        } else {
            len = l;
            index = 0;
            return 0;
        }
    }

    bool advance() {
        ++index;
        return load_more();
    }

    char get() const { return buffer[index]; }
};

static int parse_line(File_Wrapper& fw, cz::String& str, char start_char) {
    char x;
    while (1) {
        if ((x = fw.get()) != start_char) {
            return 0;
        }
        CZ_TRY(fw.advance());
        if ((x = fw.get()) != ' ') {
            return -1;
        }
        CZ_TRY(fw.advance());

        bool keep_going = true;
        while (keep_going) {
            char* newline = (char*)memchr(fw.buffer + fw.index, '\n', fw.len - fw.index);
            size_t line_len;
            if (newline) {
                line_len = newline + 1 - (fw.buffer + fw.index);
                keep_going = false;
            } else {
                line_len = fw.len - fw.index;
            }

            str.reserve(cz::heap_allocator(), line_len);
            str.append({fw.buffer + fw.index, line_len});
            fw.index += line_len;

            fw.load_more();
        }
    }
}

static int parse_file(Contents_Iterator iterator, cz::Input_File file, cz::Vector<Edit>* edits) {
    File_Wrapper fw;
    fw.file = file;
    CZ_TRY(fw.load_more());

    uint64_t iterator_line = 1;

    char x;
    while (1) {
        uint64_t line = 0;
        if (fw.load_more() == 1) {
            return 0;
        }

        while (isdigit(x = fw.get())) {
            line *= 10;
            line += x - '0';
            CZ_TRY(fw.advance());
        }

        while (x = fw.get(), x == ',' || isdigit(x)) {
            CZ_TRY(fw.advance());
        }

        bool has_remove;
        bool has_insert;
        if (x == 'a') {
            // add
            has_remove = false;
            has_insert = true;
            // for some reason the line is one lower than it should be?
            ++line;
        } else if (x == 'c') {
            // change
            has_remove = true;
            has_insert = true;
        } else if (x == 'd') {
            // delete
            has_remove = true;
            has_insert = false;
        } else {
            return -1;
        }

        while (iterator_line < line) {
            if (iterator.at_eob()) {
                break;
            }
            if (iterator.get() == '\n') {
                ++iterator_line;
            }
            iterator.advance();
        }

        while ((x = fw.get()) != '\n') {
            CZ_TRY(fw.advance());
        }
        CZ_TRY(fw.advance());

        cz::String remove_str = {};
        cz::String insert_str = {};
        Edit remove;

        edits->reserve(cz::heap_allocator(), has_insert + has_remove);

        if (has_remove) {
            if (parse_line(fw, remove_str, '<') < 0) {
                return -1;
            }

            remove.value = SSOStr::from_constant(remove_str);
            if (remove_str.len() <= SSOStr::MAX_SHORT_LEN) {
                remove_str.drop(cz::heap_allocator());
            }
            remove.position = iterator.position;
            remove.flags = Edit::REMOVE;
        }
        if (has_remove && has_insert) {
            if ((x = fw.get()) != '-') {
                return -1;
            }
            CZ_TRY(fw.advance());
            if ((x = fw.get()) != '-') {
                return -1;
            }
            CZ_TRY(fw.advance());
            if ((x = fw.get()) != '-') {
                return -1;
            }
            CZ_TRY(fw.advance());
            if ((x = fw.get()) != '\n') {
                return -1;
            }
            CZ_TRY(fw.advance());
        }
        if (has_insert) {
            if (parse_line(fw, insert_str, '>') < 0) {
                return -1;
            }

            Edit insert;
            insert.value = SSOStr::from_constant(insert_str);
            if (insert_str.len() <= SSOStr::MAX_SHORT_LEN) {
                insert_str.drop(cz::heap_allocator());
            }
            insert.position = iterator.position;
            insert.flags = Edit::INSERT;
            edits->push(insert);
        }

        // Do this after insert so that it is done before the insert when the order is flipped in
        // the callback.  We remove first because otherwise we would be removing the text we just
        // inserted!
        if (has_remove) {
            edits->push(remove);
        }
    }
}

int apply_diff_file(Client* client, Buffer* buffer, cz::Input_File file) {
    cz::Vector<Edit> edits = {};
    CZ_DEFER(edits.drop(cz::heap_allocator()));
    CZ_DEFER({
        for (size_t i = 0; i < edits.len(); ++i) {
            edits[i].value.drop(cz::heap_allocator());
        }
    });

    int ret = parse_file(buffer->contents.start(), file, &edits);
    if (ret < 0) {
        client->show_message("Error reading diff file");
        return ret;
    } else if (ret > 0) {
        client->show_message("Error diff file is truncated");
        return ret;
    }

    size_t edit_total_len = 0;
    for (size_t i = 0; i < edits.len(); ++i) {
        if (!edits[i].value.is_short()) {
            edit_total_len += edits[i].value.len();
        }
    }

    Transaction transaction;
    transaction.init(edits.len(), edit_total_len);

    for (size_t i = edits.len(); i-- > 0;) {
        Edit edit = edits[i];
        // The original is cleaned up in the deferred loop at the top.
        edit.value = edit.value.duplicate(transaction.value_allocator());
        transaction.push(edit);
    }

    transaction.commit(buffer);
    return 0;
}

void reload_file(Client* client, Buffer* buffer) {
    char diff_file[L_tmpnam];
    {
        cz::Process_Options options;
        if (!save_contents_to_temp_file(&buffer->contents, &options.std_in)) {
            client->show_message("Error saving buffer to temp file");
            return;
        }
        CZ_DEFER(options.std_in.close());

        tmpnam(diff_file);
        if (!options.std_out.open(diff_file)) {
            client->show_message("Error creating temp file to store diff in");
            return;
        }
        CZ_DEFER(options.std_out.close());

        options.std_err = cz::std_err_file();

        cz::String buffer_path = {};
        CZ_DEFER(buffer_path.drop(cz::heap_allocator()));
        buffer->get_path(cz::heap_allocator(), &buffer_path);

        cz::Str args[] = {"diff", "-" /* stdin */, buffer_path};

        cz::Process process;
        if (!process.launch_program(cz::slice(args), &options)) {
            client->show_message("Error launching diff");
            return;
        }
        process.join();
    }

    cz::Input_File file;
    if (!file.open(diff_file)) {
        client->show_message("Error opening diff file");
        return;
    }
    CZ_DEFER(file.close());

    if (apply_diff_file(client, buffer, file) == 0) {
        buffer->mark_saved();
    }
}

}
