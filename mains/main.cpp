#include <inttypes.h>
#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/str.hpp>
#include "client.hpp"
#include "command.hpp"
#include "command_macros.hpp"
#include "custom/config.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "ncurses.hpp"
#include "program_info.hpp"
#include "sdl.hpp"
#include "server.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace mag;

static int usage() {
    fprintf(stderr,
            "%s [options] [files]\n\
\n\
Mag is a text editor.\n\
\n\
Files should be one of the following forms:\n\
  FILE, FILE:LINE, or FILE:LINE:COLUMN.\n\
\n\
Options:\n\
  --help             View the help page.\n\
  --client=CLIENT    Launches a specified client.\n\
\n\
Available clients:\n"
#ifdef HAS_NCURSES
            "  ncurses   in terminal editing\n"
#endif
            "  sdl       grapical window (default)\n",
            program_name);
    return 1;
}

/// Decode the argument as one of FILE, FILE:LINE, FILE:LINE:COLUMN and then open it.
static void open_arg(Editor* editor, Client* client, cz::Str arg) {
    // If the file exists then immediately open it.
    if (cz::file::does_file_exist(arg.buffer)) {
    open:
        open_file(editor, client, arg);
        return;
    }

    // Test FILE:LINE.  If these tests fail then it's not of this form.  If the FILE component
    // doesn't exist then the file being opened just has a colon and a bunch of numbers in its path.
    const char* colon = arg.rfind(':');
    if (!colon) {
        goto open;
    }
    for (size_t i = 1; colon[i] != '\0'; ++i) {
        if (!isdigit(colon[i])) {
            goto open;
        }
    }

    uint64_t line = 0;
    sscanf(colon + 1, "%" PRIu64, &line);

    cz::String path = arg.slice_end(colon).duplicate_null_terminate(cz::heap_allocator());
    CZ_DEFER(path.drop(cz::heap_allocator()));

    if (cz::file::does_file_exist(path.buffer())) {
        // Argument is of form FILE:LINE.
        open_file(editor, client, path);

        WITH_SELECTED_BUFFER(client);
        Contents_Iterator it = start_of_line_position(buffer->contents, line);
        window->cursors[0].point = it.position;
        window->cursors[0].mark = window->cursors[0].point;
        return;
    }

    // Test FILE:LINE:COLUMN.  If these tests fail then it's not of this
    // form.  If the FILE component doesn't exist then the file being
    // opened just has a colon and a bunch of numbers in its path.
    colon = path.rfind(':');
    if (!colon) {
        goto open;
    }
    for (size_t i = 1; colon[i] != '\0'; ++i) {
        if (!isdigit(colon[i])) {
            goto open;
        }
    }

    uint64_t column = 0;
    sscanf(colon + 1, "%" PRIu64, &column);

    path.set_len(colon - path.buffer());
    path.null_terminate();

    if (cz::file::does_file_exist(path.buffer())) {
        // Argument is of form FILE:LINE:COLUMN.
        open_file(editor, client, path);

        WITH_SELECTED_BUFFER(client);
        Contents_Iterator it = start_of_line_position(buffer->contents, line);
        Contents_Iterator eol = it;
        end_of_line(&eol);
        it.advance(std::min(column, eol.position - it.position));
        window->cursors[0].point = it.position;
        window->cursors[0].mark = window->cursors[0].point;
        return;
    }

    goto open;
}

int mag_main(int argc, char** argv) {
    try {
        // Set the program name.  First try to locate the executable and then if that fails use
        // argv[0].
        cz::String program_name_storage = {};
        CZ_DEFER(program_name_storage.drop(cz::heap_allocator()));
#ifdef _WIN32
        program_name_storage.reserve(cz::heap_allocator(), MAX_PATH);
#else
        program_name_storage.reserve(cz::heap_allocator(), PATH_MAX);
#endif
        while (1) {
#ifdef _WIN32
            DWORD count =
                GetModuleFileNameA(NULL, program_name_storage.buffer(), program_name_storage.cap());
#else
            ssize_t count = readlink("/proc/self/exe", program_name_storage.buffer(),
                                     program_name_storage.cap());
#endif
            if (count <= 0) {
                // Failure.
                break;
            } else if (count <= program_name_storage.cap()) {
                // Success.
                program_name_storage.set_len(count);

                // String is already null terminated on Windows but we need to do it manually on
                // Linux.
#ifndef _WIN32
                program_name_storage.reserve(cz::heap_allocator(), 1);
                program_name_storage.null_terminate();
#endif
                break;
            } else {
                // Try again with more storage.
                program_name_storage.reserve(cz::heap_allocator(), program_name_storage.cap() * 2);
            }
        }

        // If we never set the len then we couldn't get a valid path.
        if (program_name_storage.len() == 0) {
            program_name = argv[0];
        } else {
            program_name = program_name_storage.buffer();
        }

        cz::String program_dir_ = cz::Str(program_name).duplicate(cz::heap_allocator());
        CZ_DEFER(program_dir_.drop(cz::heap_allocator()));
        cz::path::convert_to_forward_slashes(program_dir_.buffer(), program_dir_.len());
        auto program_dir_2 = cz::path::directory_component(program_dir_);
        if (program_dir_2.is_present) {
            program_dir_.set_len(program_dir_2.value.len);
            program_dir_.null_terminate();
            program_dir = program_dir_.buffer();
        } else {
            program_dir = ".";
        }

        Server server = {};
        server.editor.create();
        CZ_DEFER(server.drop());

        Buffer scratch = {};
        scratch.type = Buffer::TEMPORARY;
        scratch.name = cz::Str("*scratch*").duplicate(cz::heap_allocator());
        server.editor.create_buffer(scratch);
        server.editor.key_map = custom::create_key_map();
        server.editor.theme = custom::create_theme();

        Client client = server.make_client();
        CZ_DEFER(client.drop());

        int chosen_client = 1;
        for (int i = 1; i < argc; ++i) {
            cz::Str arg = argv[i];
            if (arg == "--help") {
                return usage();
            } else if (arg.starts_with("--client=")) {
#ifdef HAS_NCURSES
                if (arg == "--client=ncurses") {
                    chosen_client = 0;
                    continue;
                }
#endif

                if (arg == "--client=sdl") {
                    chosen_client = 1;
                    continue;
                }

                return usage();
            } else {
                open_arg(&server.editor, &client, arg);
            }
        }

        switch (chosen_client) {
#ifdef HAS_NCURSES
        case 0:
            client::ncurses::run(&server, &client);
            break;
#endif

        case 1:
            client::sdl::run(&server, &client);
            break;
        }
        return 0;
    } catch (cz::PanicReachedException& ex) {
        fprintf(stderr, "Fatal error: %s\n", ex.what());
        return 1;
    }
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(_In_ HINSTANCE hInstance,
                   _In_opt_ HINSTANCE hPrevInstance,
                   _In_ LPSTR lpCmdLine,
                   _In_ int nShowCmd) {
    return mag_main(__argc, __argv);
}
#else
int main(int argc, char** argv) {
    return mag_main(argc, argv);
}
#endif
