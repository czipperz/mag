#include "man.hpp"

#include <stdio.h>
#include <algorithm>
#include <cz/buffer_array.hpp>
#include <cz/defer.hpp>
#include <cz/directory.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include <cz/sort.hpp>
#include "client.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"

#ifndef _WIN32
#include <zlib.h>
#endif

namespace mag {
namespace man {

#ifdef _WIN32

bool man_completion_engine(Editor*, Completion_Engine_Context* context, bool is_initial_frame) {
    return false;
}
void command_man(Editor* editor, Command_Source source) {
    source.client->show_message("Error: man isn't supported on Windows");
    return;
}

#else

static bool get_man_paths(cz::Allocator path_allocator,
                          cz::Vector<cz::Str>* paths,
                          cz::Allocator paths_allocator) {
    cz::String buffer = {};
    CZ_DEFER(buffer.drop(cz::heap_allocator()));

    {
        cz::Process process;
        cz::Input_File std_out;
        CZ_DEFER(std_out.close());

        // Run manpath to get a colon delineated list of paths to look for man pages.
        {
            cz::Process_Options options;
            CZ_DEFER(options.std_out.close());

            if (!create_process_output_pipe(&options.std_out, &std_out)) {
                return false;
            }

            cz::Str args[] = {"manpath"};

            if (!process.launch_program(args, options)) {
                return false;
            }
        }

        read_to_string(std_out, cz::heap_allocator(), &buffer);

        process.join();
    }

    if (buffer.ends_with('\n')) {
        buffer.pop();
    }

    // Split by `:`.
    const char* start = buffer.start();
    while (start < buffer.end()) {
        const char* end = buffer.slice_start(start).find(':');
        if (!end) {
            end = buffer.end();
        }

        paths->reserve(paths_allocator, 1);
        paths->push(buffer.slice(start, end).clone(path_allocator));

        start = end + 1;
    }

    return true;
}

bool man_completion_engine(Editor*, Completion_Engine_Context* context, bool is_initial_frame) {
    if (!is_initial_frame) {
        return false;
    }

    context->results_buffer_array.clear();
    context->results.set_len(0);

    cz::Buffer_Array lba;
    lba.init();
    CZ_DEFER(lba.drop());

    cz::Vector<cz::Str> man_paths = {};
    CZ_DEFER(man_paths.drop(cz::heap_allocator()));
    if (!get_man_paths(lba.allocator(), &man_paths, cz::heap_allocator())) {
        return false;
    }

    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    cz::Vector<cz::Str> files = {};
    CZ_DEFER(files.drop(cz::heap_allocator()));

    cz::Allocator cbaa = context->results_buffer_array.allocator();

    for (size_t man_path_index = 0; man_path_index < man_paths.len(); ++man_path_index) {
        cz::Str directory_base = man_paths[man_path_index];

        directory.set_len(0);
        directory.reserve(cz::heap_allocator(), directory_base.len + 6);
        directory.append(directory_base);
        directory.append("/man0");
        directory.null_terminate();

        cz::Buffer_Array::Save_Point lbasp = lba.save();
        for (int man_index = 0; man_index <= 8; ++man_index) {
            lba.restore(lbasp);
            directory[directory.len() - 1] = man_index + '0';

            files.set_len(0);
            cz::Result files_result =
                cz::files(cz::heap_allocator(), lba.allocator(), directory.buffer(), &files);
            if (files_result.is_err()) {
                continue;
            }

            for (size_t i = 0; i < files.len(); ++i) {
                const char* dot = files[i].rfind('.');
                if (dot) {
                    context->results.reserve(1);
                    context->results.push(files[i].slice_end(dot).clone(cbaa));
                }
            }
        }
    }

    cz::sort(context->results);

    return true;
}

static bool decompress_gz(FILE* file, cz::String* out, cz::Allocator out_allocator) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.next_in = 0;
    stream.avail_in = 0;
    // Add together the amount of memory to be used (8..15) and detection scheme (32 = automatic).
    int window_bits = 15 + 32;
    int ret = inflateInit2(&stream, window_bits);
    if (ret != Z_OK) {
        return false;
    }

    char input_buffer[1 << 12];

    while (1) {
        size_t read_len = fread(input_buffer, 1, sizeof(input_buffer), file);
        if (read_len == 0) {
            inflateEnd(&stream);
            return true;
        }

        stream.next_in = (unsigned char*)input_buffer;
        stream.avail_in = read_len;

        do {
            out->reserve(out_allocator, 1 << 10);
            stream.next_out = (unsigned char*)out->end();
            stream.avail_out = out->cap() - out->len();
            ret = inflate(&stream, Z_NO_FLUSH);
            if (ret == Z_OK) {
                out->set_len(out->cap() - stream.avail_out);
            } else if (ret == Z_STREAM_END) {
                out->set_len(out->cap() - stream.avail_out);
                inflateEnd(&stream);
                return true;
            } else if (ret == Z_BUF_ERROR) {
                out->reserve(out_allocator, (out->cap() - out->len()) * 2);
            } else {
                fprintf(stderr, "Inflation error: %s %d\n", stream.msg, ret);
                inflateEnd(&stream);
                return false;
            }
        } while (stream.avail_out == 0);
    }
}

static bool decompress_gz_path(const char* file_name,
                               cz::String* contents,
                               cz::Allocator allocator) {
    FILE* file = fopen(file_name, "r");
    if (!file) {
        return false;
    }
    CZ_DEFER(fclose(file));

    return decompress_gz(file, contents, allocator);
}

static void lookup_specific_man_page(cz::Slice<cz::Str> man_paths,
                                     cz::Str file_to_find,
                                     cz::Vector<cz::Str>* results,
                                     cz::Buffer_Array* results_buffer_array) {
    // TODO: probably should do some input validation here.
    // file_to_find looks like "man2/man_page.2"
    cz::Str after_slash = {file_to_find.buffer + 5, file_to_find.len - 5};

    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    cz::Vector<cz::Str> files = {};
    CZ_DEFER(files.drop(cz::heap_allocator()));

    cz::Buffer_Array::Save_Point rbasp = results_buffer_array->save();

    for (size_t man_path_index = 0; man_path_index < man_paths.len; ++man_path_index) {
        cz::Str directory_base = man_paths[man_path_index];

        directory.set_len(0);
        directory.reserve(cz::heap_allocator(), directory_base.len + 6);
        directory.append(directory_base);
        directory.append("/man");
        directory.push(file_to_find[3]);
        directory.null_terminate();

        results_buffer_array->restore(rbasp);

        files.set_len(0);
        cz::Result files_result = cz::files(cz::heap_allocator(), results_buffer_array->allocator(),
                                            directory.buffer(), &files);
        if (files_result.is_err()) {
            continue;
        }

        for (size_t i = 0; i < files.len(); ++i) {
            cz::Str file = files[i];
            if (file.starts_with(after_slash)) {
                results->reserve(cz::heap_allocator(), 1);
                cz::String path = {};
                path.reserve(results_buffer_array->allocator(), directory.len() + file.len + 2);
                path.append(directory);
                path.push('/');
                path.append(file);
                path.null_terminate();
                results->push(path);
            }
        }
    }
}

static bool load_contents(cz::Slice<cz::Str> man_paths,
                          cz::Vector<cz::Str>* results,
                          cz::Buffer_Array* results_buffer_array,
                          cz::Buffer_Array::Save_Point rbasp,
                          cz::String* page,
                          cz::String* contents) {
    const char* file_name = (*results)[0].buffer;
    if (!decompress_gz_path(file_name, contents, cz::heap_allocator())) {
        return false;
    }

    while (contents->starts_with(".so ")) {
        cz::Str file_to_find = {contents->buffer() + 4, contents->len() - 5};
        results->set_len(0);
        results_buffer_array->restore(rbasp);
        lookup_specific_man_page(man_paths, file_to_find, results, results_buffer_array);

        file_name = (*results)[0].buffer;
        contents->set_len(0);
        if (!decompress_gz_path(file_name, contents, cz::heap_allocator())) {
            return false;
        }
    }

    cz::Str sfn = file_name;
    const char* fs = sfn.rfind('/');
    CZ_DEBUG_ASSERT(fs);
    ++fs;
    const char* ext = sfn.rfind('.');
    if (!ext)
        ext = sfn.end();
    sfn = sfn.slice(fs, ext);

    page->reserve(cz::heap_allocator(), sfn.len);
    page->append(sfn);

    return true;
}

static void lookup_man_page(cz::Slice<cz::Str> man_paths,
                            cz::Str man_page,
                            cz::Vector<cz::Str>* results,
                            cz::Allocator result_allocator) {
    cz::String man_page_dot = {};
    man_page_dot.reserve(cz::heap_allocator(), man_page.len + 1);
    CZ_DEFER(man_page_dot.drop(cz::heap_allocator()));
    man_page_dot.append(man_page);
    man_page_dot.push('.');

    cz::String directory = {};
    CZ_DEFER(directory.drop(cz::heap_allocator()));

    cz::Buffer_Array lba;
    lba.init();
    CZ_DEFER(lba.drop());

    cz::Vector<cz::Str> files = {};
    CZ_DEFER(files.drop(cz::heap_allocator()));

    for (size_t man_path_index = 0; man_path_index < man_paths.len; ++man_path_index) {
        cz::Str directory_base = man_paths[man_path_index];

        directory.set_len(0);
        directory.reserve(cz::heap_allocator(), directory_base.len + 6);
        directory.append(directory_base);
        directory.append("/man0");
        directory.null_terminate();

        cz::Buffer_Array::Save_Point lbasp = lba.save();
        for (int man_index = 0; man_index <= 8; ++man_index) {
            lba.restore(lbasp);
            directory[directory.len() - 1] = man_index + '0';

            files.set_len(0);
            cz::Result files_result =
                cz::files(cz::heap_allocator(), lba.allocator(), directory.buffer(), &files);
            if (files_result.is_err()) {
                continue;
            }

            for (size_t i = 0; i < files.len(); ++i) {
                cz::Str file = files[i];
                if (file.starts_with(man_page_dot)) {
                    results->reserve(cz::heap_allocator(), 1);
                    cz::String path = {};
                    path.reserve(result_allocator, directory.len() + file.len + 2);
                    path.append(directory);
                    path.push('/');
                    path.append(file);
                    path.null_terminate();
                    results->push(path);
                }
            }
        }
    }
}

static const char* find_and_load_man_page(cz::Str query, cz::String* page, cz::String* contents) {
    cz::Buffer_Array ba;
    ba.init();
    CZ_DEFER(ba.drop());

    cz::Vector<cz::Str> man_paths = {};
    CZ_DEFER(man_paths.drop(cz::heap_allocator()));

    if (!get_man_paths(ba.allocator(), &man_paths, cz::heap_allocator())) {
        return "Error: Failed to retrieve man paths";
    }

    cz::Vector<cz::Str> results = {};
    CZ_DEFER(results.drop(cz::heap_allocator()));

    cz::Buffer_Array::Save_Point basp = ba.save();
    lookup_man_page(man_paths, query, &results, ba.allocator());

    if (results.len() == 0) {
        return "Error: No matching man pages found";
    }

    if (!load_contents(man_paths, &results, &ba, basp, page, contents)) {
        return "Error: Failed to load man page";
    }

    return nullptr;
}

static void command_man_response(Editor* editor, Client* client, cz::Str query, void* data) {
    cz::String page = {};
    CZ_DEFER(page.drop(cz::heap_allocator()));
    cz::String contents = {};
    CZ_DEFER(contents.drop(cz::heap_allocator()));
    if (const char* message = find_and_load_man_page(query, &page, &contents)) {
        client->show_message(message);
        return;
    }

    char temp_file_buffer[L_tmpnam];
    if (!tmpnam(temp_file_buffer)) {
        client->show_message("Error: Failed to allocate a temp file");
        return;
    }

    {
        cz::Output_File out;
        if (!out.open(temp_file_buffer)) {
            client->show_message("Error: Failed to write temp file");
            return;
        }
        CZ_DEFER(out.close());

        size_t offset = 0;
        while (offset < contents.len()) {
            cz::Str s = contents.slice_start(offset);
            int64_t result = out.write_binary(s.buffer, s.len);
            if (result <= 0) {
                if (result == 0) {
                    break;
                } else {
                    // TODO: handle errors
                    continue;
                }
            }
            offset += result;
        }
    }

    cz::Process_Options options;
    // TODO: don't open the file twice, instead open it once in read/write mode and reset the head.
    if (!options.std_in.open(temp_file_buffer)) {
        client->show_message("Error: Failed to read temp file");
        return;
    }
    CZ_DEFER(options.std_in.close());

    cz::Input_File stdout_read;
    if (!create_process_output_pipe(&options.std_out, &stdout_read)) {
        client->show_message("Error: I/O operation failed");
        return;
    }
    CZ_DEFER(options.std_out.close());

    cz::Str args[] = {"groff", "-Tascii", "-man"};

    cz::Process process;
    if (!process.launch_program(args, options)) {
        client->show_message("Error: Couldn't show man page");
        stdout_read.close();
        return;
    }

    cz::String name = {};
    CZ_DEFER(name.drop(cz::heap_allocator()));
    name.reserve(cz::heap_allocator(), 4 + page.len());
    name.append("man ");
    name.append(page);

    cz::Arc<Buffer_Handle> handle;
    if (!find_temp_buffer(editor, client, name, {}, &handle)) {
        handle = editor->create_temp_buffer(name);
    }
    client->set_selected_buffer(handle);

    {
        WITH_BUFFER_HANDLE(handle);
        buffer->contents.remove(0, buffer->contents.len);
    }

    editor->add_asynchronous_job(
        job_process_append(handle.clone_downgrade(), process, stdout_read));
}

void command_man(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Man: ";
    dialog.completion_engine = man_completion_engine;
    dialog.response_callback = command_man_response;
    source.client->show_dialog(dialog);
}

#endif

}
}
