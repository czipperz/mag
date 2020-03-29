#include "clang_format.hpp"

#include <stdio.h>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "process.hpp"
#include "rebase.hpp"

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
                               cz::Str output_xml,
                               size_t* total_len) {
    // Todo: make this more secure.
    size_t index = 0;
    int count_greater = 0;
    for (; index < output_xml.len && count_greater < 2; ++index) {
        if (output_xml[index] == '>') {
            ++count_greater;
        }
    }
    ++index;

    while (index < output_xml.len) {
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
        for (; end < output_xml.len && output_xml[end] != '<'; ++end) {
        }
        replacement.text = stringify_xml((char*)output_xml.buffer + index, end - index);
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

static void parse_and_apply_replacements(Buffer_Handle* handle,
                                         cz::Str output_xml,
                                         size_t change_index) {
    cz::Vector<Replacement> replacements = {};
    CZ_DEFER(replacements.drop(cz::heap_allocator()));
    size_t total_len = 0;
    parse_replacements(&replacements, output_xml, &total_len);

    Transaction transaction;
    transaction.init(2 * replacements.len(), total_len);
    CZ_DEFER(transaction.drop());

    Buffer* buffer = handle->lock();
    CZ_DEFER(handle->unlock());

    cz::Slice<const Change> changes = {buffer->changes.start() + change_index,
                                       buffer->changes.len() - change_index};

    uint64_t offset = 0;
    for (size_t i = 0; i < replacements.len(); ++i) {
        Replacement* repl = &replacements[i];

        uint64_t position = repl->offset;

        Edit removal;
        // Todo: this copy is useless except for storing the length
        removal.value =
            buffer->contents.slice(transaction.value_allocator(),
                                   buffer->contents.iterator_at(position), position + repl->length);
        removal.position = position;
        removal.flags = Edit::REMOVE;

        Edit insertion;
        insertion.value.init_duplicate(transaction.value_allocator(), repl->text);
        insertion.position = position;
        insertion.flags = Edit::INSERT;

        if (offset_unmerged_edit_by_merged_changes(changes, &removal) ||
            offset_unmerged_edit_by_merged_changes(changes, &insertion)) {
            continue;
        }

        buffer->contents.slice_into(buffer->contents.iterator_at(removal.position),
                                    removal.position + repl->length, (char*)removal.value.buffer());

        removal.position += offset;
        insertion.position += offset;
        transaction.push(removal);
        transaction.push(insertion);

        offset += repl->text.len - repl->length;
    }

    transaction.commit(buffer);
}

struct Job_Clang_Format {
    Buffer_Id buffer_id;
    Process process;
    size_t change_index;
    cz::String output_xml;
};

static bool tick_job_process_append(Editor* editor, void* data) {
    Job_Clang_Format* job = (Job_Clang_Format*)data;
    while (1) {
        char buf[1024];
        ssize_t read_result = job->process.read(buf, sizeof(buf));
        if (read_result > 0) {
            job->output_xml.reserve(cz::heap_allocator(), read_result);
            job->output_xml.append({buf, (size_t)read_result});
            continue;
        } else if (read_result == 0) {
            // End of file
            job->process.join();
            job->process.destroy();

            Buffer_Handle* handle = editor->lookup(job->buffer_id);
            if (handle) {
                parse_and_apply_replacements(
                    handle, {job->output_xml.buffer(), job->output_xml.len()}, job->change_index);
            }

            job->output_xml.drop(cz::heap_allocator());
            free(data);
            return true;
        } else {
            // Nothing to read right now
            return false;
        }
    }
}

static Job job_clang_format(size_t change_index, Buffer_Id buffer_id, Process process) {
    Job_Clang_Format* data = (Job_Clang_Format*)malloc(sizeof(Job_Clang_Format));
    data->buffer_id = buffer_id;
    data->process = process;
    data->change_index = change_index;
    data->output_xml = {};

    Job job;
    job.tick = tick_job_process_append;
    job.data = data;
    return job;
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

    Process process;
    if (!process.launch_script(script.buffer(), nullptr)) {
        source.client->show_message("Error: Couldn't launch clang-format");
        return;
    }

    editor->add_job(job_clang_format(buffer->changes.len(), handle->id, process));
}

}
}
