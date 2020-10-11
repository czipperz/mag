#include "clang_format.hpp"

#include <ctype.h>
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

    if (save_contents(&buffer->contents, buffer->path.buffer())) {
        buffer->mark_saved();
    }
}

struct Clang_Format_Job_Data {
    Buffer_Id buffer_id;
    Process process;
    Input_File std_out;
    size_t change_index;
    cz::String output_xml;
};

static bool clang_format_job_tick(Editor* editor, void* _data) {
    Clang_Format_Job_Data* data = (Clang_Format_Job_Data*)_data;
    while (1) {
        char buf[1024];
        int64_t read_result = data->std_out.read(buf, sizeof(buf));
        if (read_result > 0) {
            data->output_xml.reserve(cz::heap_allocator(), read_result);
            data->output_xml.append({buf, (size_t)read_result});
            continue;
        } else if (read_result == 0) {
            // End of file
            data->std_out.close();
            data->process.join();

            Buffer_Handle* handle = editor->lookup(data->buffer_id);
            if (handle) {
                parse_and_apply_replacements(handle,
                                             {data->output_xml.buffer(), data->output_xml.len()},
                                             data->change_index);
            }

            data->output_xml.drop(cz::heap_allocator());
            free(data);
            return true;
        } else {
            // Nothing to read right now
            return false;
        }
    }
}

static void clang_format_job_kill(Editor* editor, void* _data) {
    Clang_Format_Job_Data* data = (Clang_Format_Job_Data*)_data;
    data->std_out.close();
    data->process.kill();
    data->output_xml.drop(cz::heap_allocator());
    free(data);
}

static Job job_clang_format(size_t change_index,
                            Buffer_Id buffer_id,
                            Process process,
                            Input_File std_out) {
    Clang_Format_Job_Data* data = (Clang_Format_Job_Data*)malloc(sizeof(Clang_Format_Job_Data));
    CZ_ASSERT(data);
    data->buffer_id = buffer_id;
    data->process = process;
    data->std_out = std_out;
    data->change_index = change_index;
    data->output_xml = {};

    Job job;
    job.tick = clang_format_job_tick;
    job.kill = clang_format_job_kill;
    job.data = data;
    return job;
}

void command_clang_format_buffer(Editor* editor, Command_Source source) {
    ZoneScoped;

    WITH_SELECTED_BUFFER(source.client);

    Input_File input_file;
    if (!save_contents_to_temp_file(&buffer->contents, &input_file)) {
        source.client->show_message("Error: I/O operation failed");
        return;
    }
    CZ_DEFER(input_file.close());

    cz::Str assume_filename_base = "-assume-filename=";
    cz::String assume_filename = {};
    CZ_DEFER(assume_filename.drop(cz::heap_allocator()));
    assume_filename.reserve(cz::heap_allocator(),
                            assume_filename_base.len + buffer->path.len() + 1);
    assume_filename.append(assume_filename_base);
    assume_filename.append(buffer->path);
    assume_filename.null_terminate();

    const char* args[] = {"clang-format", "-output-replacements-xml", "-style=file",
                          assume_filename.buffer(), nullptr};

    Process_Options options;
    options.std_in = input_file;
    Input_File stdout_read;
    if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
        source.client->show_message("Error: I/O operation failed");
        return;
    }
    CZ_DEFER(options.std_out.close());

    Process process;
    if (!process.launch_program(args, &options)) {
        source.client->show_message("Error: Couldn't launch clang-format");
        stdout_read.close();
        return;
    }

    editor->add_job(job_clang_format(buffer->changes.len(), handle->id, process, stdout_read));
}

}
}
