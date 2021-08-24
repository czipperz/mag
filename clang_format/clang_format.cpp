#include "clang_format.hpp"

#include <stdio.h>
#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include <cz/vector.hpp>
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"
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
    ZoneScoped;
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
        if (!cz::is_digit(str[*index])) {
            break;
        }
        *num *= 10;
        *num += str[*index] - '0';
    }
}

static void parse_replacements(cz::Vector<Replacement>* replacements,
                               cz::Str output_xml,
                               size_t* total_len,
                               cz::Str* error_line) {
    ZoneScoped;

    // TODO: make this more secure.
    size_t index = 0;
    int count_greater = 0;
    for (; index < output_xml.len && count_greater < 1; ++index) {
        if (output_xml[index] == '>') {
            ++count_greater;
        }
    }
    size_t start_of_replacements_tag = index;
    for (; index < output_xml.len && count_greater < 2; ++index) {
        if (output_xml[index] == '>') {
            ++count_greater;
        }
    }
    ++index;

    // If there is an error it looks like `<replacements xml:space='preserve'
    // incomplete_format='true' line='42'>`.  The normal case has
    // `incomplete_format='false'` and `line` is not specified.
    cz::Str replacements_tag = output_xml.slice(start_of_replacements_tag, index);
    const char* incomplete_format = replacements_tag.find("incomplete_format");
    if (incomplete_format) {
        size_t index = incomplete_format - replacements_tag.buffer + strlen("incomplete_format");
        if (advance(&index, replacements_tag, "='true' line='")) {
            size_t end = index;
            for (; end < replacements_tag.len; ++end) {
                if (!cz::is_digit(replacements_tag[end])) {
                    break;
                }
            }

            if (end > index) {
                *error_line = replacements_tag.slice(index, end);
            }
        }
    }

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

static void parse_and_apply_replacements(Asynchronous_Job_Handler* handler,
                                         Buffer_Handle* handle,
                                         cz::Str output_xml,
                                         size_t change_index,
                                         cz::Str* error_line) {
    ZoneScoped;

    cz::Vector<Replacement> replacements = {};
    CZ_DEFER(replacements.drop(cz::heap_allocator()));
    size_t total_len = 0;
    parse_replacements(&replacements, output_xml, &total_len, error_line);

    WITH_BUFFER_HANDLE(handle);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Slice<const Change> changes = buffer->changes.slice_start(change_index);

    uint64_t offset = 0;
    for (size_t i = 0; i < replacements.len; ++i) {
        Replacement* repl = &replacements[i];

        uint64_t position = repl->offset;

        Edit removal;
        // TODO: this copy is useless except for storing the length
        removal.value =
            buffer->contents.slice(transaction.value_allocator(),
                                   buffer->contents.iterator_at(position), position + repl->length);
        removal.position = position;
        removal.flags = Edit::REMOVE;

        Edit insertion;
        insertion.value = SSOStr::as_duplicate(transaction.value_allocator(), repl->text);
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

    transaction.commit(handler);

    if (error_line->len == 0) {
        save_buffer(buffer);
    }
}

struct Clang_Format_Job_Data {
    cz::Arc_Weak<Buffer_Handle> buffer_handle;
    cz::Process process;
    cz::Carriage_Return_Carry carry;
    cz::Input_File std_out;
    size_t change_index;
    cz::String output_xml;
};

static Job_Tick_Result clang_format_job_tick(Asynchronous_Job_Handler* handler, void* _data) {
    ZoneScoped;

    Clang_Format_Job_Data* data = (Clang_Format_Job_Data*)_data;
    char buf[1024];
    for (int reads = 0; reads < 128; ++reads) {
        int64_t read_result = data->std_out.read_text(buf, sizeof(buf), &data->carry);
        if (read_result > 0) {
            data->output_xml.reserve(cz::heap_allocator(), read_result);
            data->output_xml.append({buf, (size_t)read_result});
            continue;
        } else if (read_result == 0) {
            // End of file
            data->std_out.close();
            data->process.join();

            cz::Arc<Buffer_Handle> handle;
            if (data->buffer_handle.upgrade(&handle)) {
                CZ_DEFER(handle.drop());
                cz::Str error_line = {};
                parse_and_apply_replacements(handler, handle.get(), data->output_xml,
                                             data->change_index, &error_line);

                if (error_line.len > 0) {
                    cz::String message = {};
                    CZ_DEFER(message.drop(cz::heap_allocator()));
                    cz::Str prefix = "Error: clang-format failed on line ";
                    message.reserve(cz::heap_allocator(), prefix.len + error_line.len);
                    message.append(prefix);
                    message.append(error_line);

                    handler->show_message(message);
                }
            }

            data->output_xml.drop(cz::heap_allocator());
            cz::heap_allocator().dealloc(data);
            return Job_Tick_Result::FINISHED;
        } else {
            // Nothing to read right now
            return reads > 0 ? Job_Tick_Result::MADE_PROGRESS : Job_Tick_Result::STALLED;
        }
    }

    // Let another job run.
    return Job_Tick_Result::MADE_PROGRESS;
}

static void clang_format_job_kill(void* _data) {
    Clang_Format_Job_Data* data = (Clang_Format_Job_Data*)_data;
    data->std_out.close();
    data->process.kill();
    data->output_xml.drop(cz::heap_allocator());
    data->buffer_handle.drop();
    cz::heap_allocator().dealloc(data);
}

static Asynchronous_Job job_clang_format(size_t change_index,
                                         cz::Arc_Weak<Buffer_Handle> buffer_handle,
                                         cz::Process process,
                                         cz::Input_File std_out) {
    Clang_Format_Job_Data* data = cz::heap_allocator().alloc<Clang_Format_Job_Data>();
    CZ_ASSERT(data);
    data->buffer_handle = buffer_handle;
    data->process = process;
    data->carry = {};
    data->std_out = std_out;
    data->change_index = change_index;
    data->output_xml = {};

    Asynchronous_Job job;
    job.tick = clang_format_job_tick;
    job.kill = clang_format_job_kill;
    job.data = data;
    return job;
}

void command_clang_format_buffer(Editor* editor, Command_Source source) {
    ZoneScoped;

    WITH_SELECTED_BUFFER(source.client);

    cz::Input_File input_file;
    // Use binary because we want no carriage returns on Windows because they make it hard to
    // process replacements.
    if (!save_contents_to_temp_file(&buffer->contents, &input_file,
                                    /*use_carriage_returns=*/false)) {
        source.client->show_message("Error: I/O operation failed");
        return;
    }
    CZ_DEFER(input_file.close());

    cz::Str assume_filename_base = "-assume-filename=";
    cz::String assume_filename = {};
    CZ_DEFER(assume_filename.drop(cz::heap_allocator()));
    assume_filename.reserve(cz::heap_allocator(), assume_filename_base.len);
    assume_filename.append(assume_filename_base);
    buffer->get_path(cz::heap_allocator(), &assume_filename);

    cz::Str args[] = {"clang-format", "-output-replacements-xml", "-style=file", assume_filename};

    cz::Process_Options options;
    options.std_in = input_file;
    cz::Input_File stdout_read;
    if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
        source.client->show_message("Error: I/O operation failed");
        return;
    }
    stdout_read.set_non_blocking();
    CZ_DEFER(options.std_out.close());

    cz::Process process;
    if (!process.launch_program(args, options)) {
        source.client->show_message("Error: Couldn't launch clang-format");
        stdout_read.close();
        return;
    }

    editor->add_asynchronous_job(
        job_clang_format(buffer->changes.len, handle.clone_downgrade(), process, stdout_read));
}

}
}
