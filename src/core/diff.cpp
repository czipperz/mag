#include "diff.hpp"

#include <stdio.h>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include "core/buffer.hpp"
#include "core/client.hpp"
#include "core/edit.hpp"
#include "core/file.hpp"
#include "core/transaction.hpp"

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

    int advance() {
        ++index;
        return load_more();
    }

    char get() const { return buffer[index]; }
};

static int eat(File_Wrapper& fw, char ch) {
    char x = fw.get();
    if (x != ch)
        return -1;
    return fw.advance();
}

static int eat_no_newline(File_Wrapper& fw, cz::String& str) {
    cz::Str expected = "\\ No newline at end of file";
    for (size_t i = 0; i < expected.len; ++i) {
        int result = eat(fw, expected[i]);
        if (result != 0) {
            return result;
        }
    }

    if (!str.ends_with('\n')) {
        return -1;
    }
    str.pop();

    return eat(fw, '\n');
}

static int parse_line(File_Wrapper& fw, cz::String& str, char start_char) {
    int result;
    char x;
    while (1) {
        if ((x = fw.get()) != start_char) {
            if (x == '\\') {
                return eat_no_newline(fw, str);
            }
            return 0;
        }
        result = fw.advance();
        if (result != 0)
            return result;

        result = eat(fw, ' ');
        if (result != 0)
            return result;

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

            result = fw.load_more();
            if (result != 0)
                return result;
        }
    }
}

static int parse_file(Contents_Iterator iterator, cz::Input_File file, cz::Vector<Edit>* edits) {
    File_Wrapper fw;
    fw.file = file;

    int result = fw.load_more();
    if (result < 0) {
        return result;
    } else if (result == 1) {
        return 0;
    }

    uint64_t iterator_line = 1;

    char x;
    while (1) {
        uint64_t line = 0;
        int result = fw.load_more();
        if (result == 1) {
            return 0;
        } else if (result < 0) {
            return result;
        }

        while (cz::is_digit(x = fw.get())) {
            line *= 10;
            line += x - '0';
            result = fw.advance();
            if (result != 0)
                return result;
        }

        while (x = fw.get(), x == ',' || cz::is_digit(x)) {
            result = fw.advance();
            if (result != 0)
                return result;
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
            result = fw.advance();
            if (result != 0)
                return result;
        }
        result = fw.advance();
        if (result != 0)
            return result;

        cz::String remove_str = {};
        cz::String insert_str = {};
        Edit remove;

        edits->reserve(cz::heap_allocator(), has_insert + has_remove);

        if (has_remove) {
            if (parse_line(fw, remove_str, '<') < 0) {
                return -1;
            }

            remove.value = SSOStr::from_constant(remove_str);
            if (remove_str.len <= SSOStr::MAX_SHORT_LEN) {
                remove_str.drop(cz::heap_allocator());
            }
            remove.position = iterator.position;
            remove.flags = Edit::REMOVE;
        }
        if (has_remove && has_insert) {
            result = eat(fw, '-');
            if (result != 0)
                return result;

            result = eat(fw, '-');
            if (result != 0)
                return result;

            result = eat(fw, '-');
            if (result != 0)
                return result;

            result = eat(fw, '\n');
            if (result != 0)
                return result;
        }
        if (has_insert) {
            if (parse_line(fw, insert_str, '>') < 0) {
                return -1;
            }

            Edit insert;
            insert.value = SSOStr::from_constant(insert_str);
            if (insert_str.len <= SSOStr::MAX_SHORT_LEN) {
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

const char* apply_diff_file(Buffer* buffer, cz::Input_File file) {
    cz::Vector<Edit> edits = {};
    CZ_DEFER(edits.drop(cz::heap_allocator()));
    CZ_DEFER({
        for (size_t i = 0; i < edits.len; ++i) {
            edits[i].value.drop(cz::heap_allocator());
        }
    });

    int ret = parse_file(buffer->contents.start(), file, &edits);
    if (ret < 0) {
        return "Error reading diff file";
    } else if (ret > 0) {
        return "Error diff file is truncated";
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    for (size_t i = edits.len; i-- > 0;) {
        Edit edit = edits[i];
        // The original is cleaned up in the deferred loop at the top.
        edit.value = edit.value.clone(transaction.value_allocator());
        transaction.push(edit);
    }

    const char* message = nullptr;
    transaction.commit(&message);
    return message;
}

const char* reload_file(Buffer* buffer) {
    bool old_read_only = buffer->read_only;
    buffer->read_only = false;
    CZ_DEFER(buffer->read_only = old_read_only);

    if (buffer->type == Buffer::DIRECTORY) {
        if (!reload_directory_buffer(buffer)) {
            return "Couldn't reload directory";
        }
        return nullptr;
    }

    char diff_file[L_tmpnam];
    {
        if (!tmpnam(diff_file)) {
            // Couldn't create a temp file.
            return "Error: couldn't create a temp file";
        }

        cz::Process_Options options;
#ifdef _WIN32
        options.hide_window = true;
#endif
        if (!save_buffer_to_temp_file(buffer, &options.std_in)) {
            return "Error saving buffer to temp file";
        }
        CZ_DEFER(options.std_in.close());

        if (!options.std_out.open(diff_file)) {
            return "Error creating temp file to store diff in";
        }
        CZ_DEFER(options.std_out.close());

        options.std_err = cz::std_err_file();

        cz::String buffer_path = {};
        CZ_DEFER(buffer_path.drop(cz::heap_allocator()));
        buffer->get_path(cz::heap_allocator(), &buffer_path);

        cz::Str args[] = {"diff", "-" /* stdin */, buffer_path};

        cz::Process process;
        if (!process.launch_program(args, options)) {
            return "Error launching diff";
        }
        process.join();
    }

    cz::Input_File file;
    if (!file.open(diff_file)) {
        return "Error opening diff file";
    }
    CZ_DEFER(file.close());

    const char* error = apply_diff_file(buffer, file);
    if (!error) {
        buffer->mark_saved();
    }
    return error;
}

}
