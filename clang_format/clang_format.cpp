#include "clang_format.hpp"

#include <stdio.h>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "process.hpp"

namespace mag {
namespace clang_format {

struct Replacement {
    uint64_t offset;
    uint64_t length;
    cz::Str text;
};

static bool advance(size_t* index, cz::Str str, cz::Str expected) {
    for (size_t i = 0; i < expected.len; ++i, ++*index) {
        if (*index == str.len) {
            return false;
        }
        if (str[*index] != expected[i]) {
            return false;
        }
    }
    return true;
}

static cz::Str stringify_xml(char* buffer, size_t len) {
    for (size_t i = 0; i < len;) {
        size_t end;
        if (advance(&(end = i), {buffer, len}, "&lt;")) {
            buffer[i] = '<';
            ++i;
            memmove(buffer + i, buffer + end, len - end);
            len -= end - i;
        } else if (advance(&(end = i), {buffer, len}, "&gt;")) {
            buffer[i] = '>';
            ++i;
            memmove(buffer + i, buffer + end, len - end);
            len -= end - i;
        } else if (advance(&(end = i), {buffer, len}, "&#10;")) {
            buffer[i] = '\n';
            ++i;
            memmove(buffer + i, buffer + end, len - end);
            len -= end - i;
        } else {
            ++i;
        }
    }
    return {buffer, len};
}

static void parse_number(uint64_t* num, size_t* index, cz::Str str) {
    *num = 0;
    for (; *index < str.len; ++*index) {
        if (!isdigit(str[*index])) {
            break;
        }
        *num *= 10;
        *num += str[*index] - '0';
    }
}

static void parse_replacements(cz::Vector<Replacement>* replacements,
                               cz::String output_xml,
                               size_t* total_len) {
    // Todo: make this more secure.
    size_t index = 0;
    int count_greater = 0;
    for (; index < output_xml.len() && count_greater < 2; ++index) {
        if (output_xml[index] == '>') {
            ++count_greater;
        }
    }
    ++index;

    while (index < output_xml.len()) {
        Replacement replacement;
        if (!advance(&index, output_xml, "<replacement offset='")) {
            break;
        }
        parse_number(&replacement.offset, &index, output_xml);

        if (!advance(&index, output_xml, "' length='")) {
            break;
        }
        parse_number(&replacement.length, &index, output_xml);

        if (!advance(&index, output_xml, "'>")) {
            break;
        }

        size_t end = index;
        for (; end < output_xml.len() && output_xml[end] != '<'; ++end) {
        }
        replacement.text = stringify_xml(output_xml.buffer() + index, end - index);
        *total_len += replacement.text.len;
        *total_len += replacement.length;
        replacements->reserve(cz::heap_allocator(), 1);
        replacements->push(replacement);

        if (!advance(&end, output_xml, "</replacement>\n")) {
            break;
        }
        index = end;
    }
}

static void apply_replacement(Replacement* repl,
                              Transaction* transaction,
                              Buffer* buffer,
                              uint64_t* offset) {
    uint64_t position = repl->offset;

    Edit removal;
    removal.value =
        buffer->contents.slice(transaction->value_allocator(),
                               buffer->contents.iterator_at(position), position + repl->length);
    removal.position = position + *offset;
    removal.flags = Edit::REMOVE;
    transaction->push(removal);

    Edit insertion;
    insertion.value.init_duplicate(transaction->value_allocator(), repl->text);
    insertion.position = position + *offset;
    insertion.flags = Edit::INSERT;
    transaction->push(insertion);

    *offset += repl->text.len - repl->length;
}

void command_clang_format_buffer(Editor* editor, Command_Source source) {
    ZoneScoped;

    WITH_SELECTED_BUFFER(source.client);

    char temp_file_buffer[L_tmpnam];
    tmpnam(temp_file_buffer);
    save_contents(&buffer->contents, temp_file_buffer);
    cz::Str temp_file_str = temp_file_buffer;

    cz::String script = {};
    CZ_DEFER(script.drop(cz::heap_allocator()));
    cz::Str base = "clang-format -output-replacements-xml -style=file -assume-filename=";
    script.reserve(cz::heap_allocator(), base.len + buffer->path.len() + 3 + temp_file_str.len + 1);
    script.append(base);
    script.append(buffer->path);
    script.append(" < ");
    script.append(temp_file_str);
    script.null_terminate();

    cz::String output_xml = {};
    CZ_DEFER(output_xml.drop(cz::heap_allocator()));

    // Todo: make this a job
    Process process;
    if (!process.launch_script(script.buffer(), nullptr)) {
        source.client->show_message("Error: Couldn't launch clang-format");
        return;
    }
    process.read_to_string(cz::heap_allocator(), &output_xml);
    int return_value = process.join();
    process.destroy();
    if (return_value != 0) {
        source.client->show_message("Error: Syntax error");
        return;
    }

    cz::Vector<Replacement> replacements = {};
    CZ_DEFER(replacements.drop(cz::heap_allocator()));
    size_t total_len = 0;
    parse_replacements(&replacements, output_xml, &total_len);

    Transaction transaction;
    transaction.init(2 * replacements.len(), total_len);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;
    for (size_t i = 0; i < replacements.len(); ++i) {
        Replacement* repl = &replacements[i];
        apply_replacement(repl, &transaction, buffer, &offset);
    }

    transaction.commit(buffer);
}

}
}
