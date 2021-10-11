#include "blame.hpp"

#include <cz/date.hpp>
#include <cz/format.hpp>
#include <cz/heap_string.hpp>
#include <cz/parse.hpp>
#include <cz/process.hpp>
#include "command_macros.hpp"
#include "file.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "version_control.hpp"

namespace mag {
namespace version_control {

///////////////////////////////////////////////////////////////////////////////
// blame job
///////////////////////////////////////////////////////////////////////////////

struct Job_Blame_Append_Data {
    cz::Arc_Weak<Buffer_Handle> handle;
    cz::Process process;
    cz::Input_File stdout_read;
    cz::String buffer;
};

enum Parse_Status {
    Parse_Ok,
    Parse_Error,
    Parse_Eob,
};
static Parse_Status parse_commit_info(cz::Str elem,
                                      cz::Str* hash,
                                      time_t* commit_time,
                                      cz::Str* line,
                                      size_t* offset);

static Job_Tick_Result job_blame_append_tick(Asynchronous_Job_Handler* handler, void* _data) {
    Job_Blame_Append_Data* data = (Job_Blame_Append_Data*)_data;
    bool done = false;
    while (1) {
        data->buffer.reserve(cz::heap_allocator(), 4096);
        int64_t result = data->stdout_read.read(data->buffer.end(), data->buffer.remaining());
        if (result <= 0) {
            if (result == 0) {
                done = true;
            }
            break;
        }
        data->buffer.len += result;
    }

    size_t index = 0;
    {
        cz::Arc<Buffer_Handle> handle;
        if (!data->handle.upgrade(&handle)) {
            goto cleanup;
        }
        CZ_DEFER(handle.drop());
        WITH_BUFFER_HANDLE(handle);

        while (index < data->buffer.len) {
            cz::Str elem = data->buffer.slice_start(index);
            cz::Str hash, line;
            time_t commit_time_u64;
            Parse_Status status = parse_commit_info(elem, &hash, &commit_time_u64, &line, &index);
            if (status != Parse_Ok) {
                if (status == Parse_Error) {
                    handler->show_message("Failed to parse git blame");
                    goto cleanup;
                }
                break;
            }

            if (hash != "0000000000000000000000000000000000000000") {
                cz::Date date = cz::time_t_to_date_utc(commit_time_u64);
                char header[21];
                snprintf(header, sizeof(header), "%.8s %.4d-%.2d-%.2d ", hash.buffer, date.year,
                         date.month, date.day_of_month);
                buffer->contents.append(header);
            } else {
                buffer->contents.append("                    ");
            }

            buffer->contents.append(line);
        }
    }

    if (!done) {
        if (index == 0) {
            return Job_Tick_Result::STALLED;
        }

        data->buffer.remove_range(0, index);
        return Job_Tick_Result::MADE_PROGRESS;
    }

cleanup:
    data->handle.drop();
    data->process.kill();
    data->stdout_read.close();
    data->buffer.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
    return Job_Tick_Result::FINISHED;
}

static Parse_Status parse_commit_info(cz::Str elem,
                                      cz::Str* hash,
                                      time_t* commit_time,
                                      cz::Str* line,
                                      size_t* offset) {
    // Get metadata section.
    const char* metadata_end = elem.find("\n\t");
    if (!metadata_end)
        return Parse_Eob;
    cz::Str metadata = elem.slice_end(metadata_end + 1);

    // Parse *line.
    cz::Str line_start = elem.slice_start(metadata_end + 2);
    cz::Str rest;
    if (!line_start.split_after('\n', line, &rest))
        return Parse_Eob;
    *offset += rest.buffer - elem.buffer;

    // Parse hash.
    size_t i = 0;
    if (metadata.len < i + 40)
        return Parse_Error;
    for (; i < 40; ++i) {
        if (!cz::is_hex_digit(elem[i])) {
            return Parse_Error;
        }
    }
    *hash = elem.slice_end(40);

    cz::Str commit_time_query = "\ncommitter-time ";
    const char* commit_time_start = metadata.find(commit_time_query);
    if (!commit_time_start)
        return Parse_Error;

    cz::Str commit_time_str = metadata.slice_start(commit_time_start + commit_time_query.len);
    commit_time_str = commit_time_str.slice_end('\n');
    if (cz::parse(commit_time_str, commit_time) != commit_time_str.len) {
        return Parse_Error;
    }

    return Parse_Ok;
}

static void job_blame_append_kill(void* _data) {
    Job_Blame_Append_Data* data = (Job_Blame_Append_Data*)_data;
    data->handle.drop();
    data->process.kill();
    data->stdout_read.close();
    data->buffer.drop(cz::heap_allocator());
    cz::heap_allocator().dealloc(data);
}

static Asynchronous_Job job_blame_append(cz::Arc_Weak<Buffer_Handle> handle,
                                         cz::Process process,
                                         cz::Input_File stdout_read) {
    Job_Blame_Append_Data* data = cz::heap_allocator().alloc<Job_Blame_Append_Data>();
    data->handle = handle;
    data->process = process;
    data->stdout_read = stdout_read;
    data->buffer = {};

    Asynchronous_Job job;
    job.tick = job_blame_append_tick;
    job.kill = job_blame_append_kill;
    job.data = data;
    return job;
}

///////////////////////////////////////////////////////////////////////////////
// command_blame
///////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_blame);
void command_blame(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    cz::String root = {};
    CZ_DEFER(root.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            source.client->show_message("Error: file has no path");
            return;
        }

        if (!get_root_directory(editor, source.client, buffer->directory.buffer,
                                cz::heap_allocator(), &root)) {
            source.client->show_message("Error: couldn't find vc root");
            return;
        }
    }

    cz::Heap_String buffer_name = cz::format("git blame ", path);
    CZ_DEFER(buffer_name.drop());

    cz::Option<cz::Str> wd = {root};
    cz::Arc<Buffer_Handle> handle;
    if (!find_temp_buffer(editor, source.client, buffer_name, wd, &handle)) {
        handle = editor->create_temp_buffer(buffer_name, wd);
    }

    {
        WITH_BUFFER_HANDLE(handle);
        buffer->contents.remove(0, buffer->contents.len);
    }

    source.client->set_selected_buffer(handle);

    {
        cz::Process_Options options;
        options.working_directory = root.buffer;

        cz::Input_File stdout_read;
        if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
            source.client->show_message("Error: I/O operation failed");
            return;
        }
        stdout_read.set_non_blocking();
        CZ_DEFER(options.std_out.close());

        options.std_err = options.std_out;

        cz::Str args[] = {"git", "blame", "--line-porcelain", "--", path};
        cz::Process process;
        if (!process.launch_program(args, options)) {
            source.client->show_message("Git error");
            stdout_read.close();
            return;
        }

        editor->add_asynchronous_job(
            job_blame_append(handle.clone_downgrade(), process, stdout_read));
        return;
    }
}

///////////////////////////////////////////////////////////////////////////////
// git_blame_next_token
///////////////////////////////////////////////////////////////////////////////

namespace blame {
enum {
    Line_Hash = 0,
    Line_Date = 1,
    Line_Contents = 2,
};

struct State {
    uint64_t top : 2;
    uint64_t rest : 62;
};
}
using namespace blame;

bool git_blame_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state_) {
    State* state = (State*)state_;
retry:
    switch (state->top) {
    case Line_Hash: {
        if (looking_at(*iterator, '\n'))
            iterator->advance();
        if (iterator->position + 8 > iterator->contents->len)
            return false;

        if (looking_at(*iterator, "                    ")) {
            iterator->advance(20);
            state->top = Line_Contents;
            goto retry;
        }

        token->type = Token_Type::BLAME_COMMIT;
        token->start = iterator->position;
        iterator->advance(8);
        token->end = iterator->position;
        state->top = Line_Date;
        return true;
    }

    case Line_Date: {
        if (looking_at(*iterator, ' '))
            iterator->advance();
        if (iterator->position + 10 > iterator->contents->len)
            return false;

        token->type = Token_Type::BLAME_DATE;
        token->start = iterator->position;
        iterator->advance(10);
        token->end = iterator->position;
        state->top = Line_Contents;
        return true;
    }

    case Line_Contents: {
        if (looking_at(*iterator, ' '))
            iterator->advance();

        token->type = Token_Type::BLAME_CONTENTS;
        token->start = iterator->position;
        end_of_line(iterator);
        token->end = iterator->position;
        state->top = Line_Hash;
        return true;
    }

    default:
        return false;
    }
}

}
}
