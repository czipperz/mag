#include "rust_commands.hpp"

#include <cz/find_file.hpp>
#include <cz/format.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include "core/command.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/job.hpp"
#include "core/movement.hpp"

namespace mag {
namespace rust {

REGISTER_COMMAND(command_extract_variable);

namespace {

// TODO: deduplicate with Process_Show_Message_With_File_Contents_Job lol

struct Rustfmt_Process_Show_Message_With_File_Contents_Job {
    cz::Process process;
    cz::Carriage_Return_Carry carry;
    cz::Input_File file;
    cz::String string;
    size_t prefix_length;
};
}

static void process_show_message_with_file_contents_job_kill(void* _data) {
    Rustfmt_Process_Show_Message_With_File_Contents_Job* data =
        (Rustfmt_Process_Show_Message_With_File_Contents_Job*)_data;
    data->process.kill();
    data->file.close();
    data->string.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

static Job_Tick_Result process_show_message_with_file_contents_job_tick(
    Asynchronous_Job_Handler* handler,
    void* _data) {
    Rustfmt_Process_Show_Message_With_File_Contents_Job* data =
        (Rustfmt_Process_Show_Message_With_File_Contents_Job*)_data;

    (void)cz::read_to_string(data->file, cz::heap_allocator(), &data->string);

    int ret;
    if (data->process.try_join(&ret)) {
        if (data->string.len > data->prefix_length) {
            size_t newline = data->string.find_index('\n');
            cz::Str error = data->string.slice_end(newline);

            if (newline < data->string.len)
                ++newline;

            cz::Str path = data->string.slice_start(newline).slice_end(
                data->string.slice_start(newline).find_index('\n'));
            if (path.starts_with(" --> "))
                path = path.slice_start(strlen(" --> "));
            if (path.starts_with("\\\\?\\"))
                path = path.slice_start(strlen("\\\\?\\"));

            cz::Heap_String message =
                cz::format(data->string.slice_end(data->prefix_length), path, ": ", error);
            CZ_DEFER(message.drop());
            cz::path::convert_to_forward_slashes(message.buffer + data->prefix_length, path.len);
            handler->show_message(message);
        }

        data->file.close();
        data->string.drop(cz::heap_allocator());
        cz::heap_allocator().dealloc(data);
        return Job_Tick_Result::FINISHED;
    } else {
        return Job_Tick_Result::STALLED;
    }
}

static Asynchronous_Job job_rustfmt_process_show_message_with_file_contents(
    cz::Process process,
    cz::Input_File file,
    cz::Str message_prefix) {
    Rustfmt_Process_Show_Message_With_File_Contents_Job* data =
        cz::heap_allocator().alloc<Rustfmt_Process_Show_Message_With_File_Contents_Job>();
    CZ_ASSERT(data);
    data->process = process;
    data->carry = {};
    data->file = file;
    data->string = {};
    data->string.reserve(cz::heap_allocator(), cz::max((size_t)1024, message_prefix.len));
    data->string.append(message_prefix);
    data->prefix_length = message_prefix.len;

    Asynchronous_Job job;
    job.tick = process_show_message_with_file_contents_job_tick;
    job.kill = process_show_message_with_file_contents_job_kill;
    job.data = data;
    return job;
}

void command_extract_variable(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (!window->show_marks) {
        source.client->show_message("Must select a region");
        return;
    }

    Transaction transaction = {};
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    window->show_marks = 0;

    int64_t offset = 0;

    Contents_Iterator it = buffer->contents.start();

    // Delete all the regions.
    for (size_t c = 0; c < window->cursors.len; ++c) {
        Cursor* cursor = &window->cursors[c];
        it.advance_to(cursor->start());
        Edit remove_region;
        remove_region.value =
            buffer->contents.slice(transaction.value_allocator(), it, cursor->end());
        remove_region.position = cursor->start() + offset;
        remove_region.flags = Edit::REMOVE;
        transaction.push(remove_region);
        offset -= remove_region.value.len();
    }

    //
    // Insert the template.
    //
    it.retreat_to(window->cursors[0].start());
    start_of_line(&it);
    Contents_Iterator st = it;
    forward_through_whitespace(&st);

    offset = 0;

    Edit insert_indent;
    insert_indent.value = buffer->contents.slice(transaction.value_allocator(), it, st.position);
    insert_indent.position = it.position + offset;
    insert_indent.flags = Edit::INSERT;
    transaction.push(insert_indent);
    offset += insert_indent.value.len();

    Edit insert_prefix;
    insert_prefix.value = SSOStr::from_constant("let  = ");
    insert_prefix.position = it.position + offset;
    insert_prefix.flags = Edit::INSERT;
    transaction.push(insert_prefix);
    offset += insert_prefix.value.len();

    Edit insert_region;
    insert_region.value = transaction.edits[0].value;
    insert_region.position = it.position + offset;
    insert_region.flags = Edit::INSERT;
    transaction.push(insert_region);
    offset += insert_region.value.len();

    Edit insert_suffix;
    insert_suffix.value = SSOStr::from_constant(";\n");
    insert_suffix.position = it.position + offset;
    insert_suffix.flags = Edit::INSERT;
    transaction.push(insert_suffix);
    offset += insert_suffix.value.len();

    if (!transaction.commit(source.client))
        return;

    //
    // Fix the cursors
    //

    // The template gets a cursor.
    Cursor new_cursor = window->cursors[0];
    new_cursor.point = new_cursor.mark = insert_prefix.position + strlen("let ");
    window->cursors.insert(0, new_cursor);
    window->selected_cursor = 0;

    // Adjust cursors around the template.
    uint64_t offset2 = 0;
    for (size_t c = 1; c < window->cursors.len; ++c) {
        Cursor* cursor = &window->cursors[c];
        uint64_t removed = cursor->end() - cursor->start();
        cursor->point = cursor->start() + offset - offset2;
        cursor->mark = cursor->point;
        offset2 += removed;
    }

    // We manually fixed the cursors so the window doesn't need to do any updates.
    window->change_index = buffer->changes.len;
}

REGISTER_COMMAND(command_rust_format_buffer);
void command_rust_format_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    cz::Process_Options options;
    options.working_directory = buffer->directory.buffer;

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    if (!buffer->get_path(cz::heap_allocator(), &path)) {
        source.client->show_message("Can only format file buffers");
        return;
    }

    // Try to get the edition from Cargo.toml.
    cz::Str edition = {};
    cz::String toml_path = path.clone(cz::heap_allocator());
    CZ_DEFER(toml_path.drop(cz::heap_allocator()));
    cz::String toml_contents = {};
    CZ_DEFER(toml_contents.drop(cz::heap_allocator()));
    if (cz::find_file_up(cz::heap_allocator(), &toml_path, "Cargo.toml")) {
        if (cz::read_to_string(toml_path.buffer, cz::heap_allocator(), &toml_contents)) {
            cz::Str pattern = "\nedition = \"";
            if (const char* point = toml_contents.find(pattern)) {
                cz::Str ed = toml_contents.slice_start(point + pattern.len);
                // Check that the edition string is terminated and capture it.
                const char* end = ed.find('"');
                const char* nl = ed.find('\n');
                if (end && end < nl)
                    edition = ed.slice_end(end);
            }
        }
    }

    cz::Input_File std_err;
    if (!cz::create_process_output_pipe(&options.std_err, &std_err)) {
        source.client->show_message("Couldn't create output pipe");
        return;
    }
    CZ_DEFER(options.std_err.close());
    CZ_DEFER(std_err.close());

    if (!std_err.set_non_blocking()) {
        source.client->show_message("Couldn't set output reader to non-blocking");
        return;
    }

    // Run rustfmt on the path.  If we found an edition then use it.
    cz::Process process;
    bool success = 0;
    if (edition.len > 0) {
        cz::String edition_arg = cz::format("--edition=", edition);
        CZ_DEFER(edition_arg.drop(cz::heap_allocator()));
        cz::Str args[] = {"rustfmt", edition_arg, path};
        success = process.launch_program(args, options);
    } else {
        cz::Str args[] = {"rustfmt", path};
        success = process.launch_program(args, options);
    }

    if (!success) {
        source.client->show_message("Shell error");
        return;
    }

    editor->add_asynchronous_job(
        job_rustfmt_process_show_message_with_file_contents(process, std_err, "rustfmt failed: "));
    std_err = {};
}

}
}
