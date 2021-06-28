#include <inttypes.h>
#include <stdio.h>
#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/str.hpp>
#include <cz/working_directory.hpp>
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

static void open_file_tiling(Editor* editor,
                             Client* client,
                             cz::Str arg,
                             uint32_t* opened_count,
                             uint64_t line,
                             uint64_t column) {
    if (arg.len == 0) {
        client->show_message(editor, "File path must not be empty");
        return;
    }

    // Open the file asynchronously.
    cz::String path = standardize_path(cz::heap_allocator(), arg);
    editor->add_asynchronous_job(job_open_file(path, line, column, *opened_count));
    ++*opened_count;
}

/// Decode the argument as one of FILE, FILE:LINE, FILE:LINE:COLUMN and then open it.
static void open_arg(Editor* editor, Client* client, cz::Str arg, uint32_t* opened_count) {
    ZoneScoped;

    // If the file exists then immediately open it.
    if (cz::file::exists(arg.buffer)) {
    open:
        open_file_tiling(editor, client, arg, opened_count, 0, 0);
        return;
    }

    // Test FILE:LINE.  If these tests fail then it's not of this form.  If the FILE component
    // doesn't exist then the file being opened just has a colon and a bunch of numbers in its path.
    const char* colon = arg.rfind(':');
    if (!colon) {
        goto open;
    }
    for (size_t i = 1; colon[i] != '\0'; ++i) {
        if (!cz::is_digit(colon[i])) {
            goto open;
        }
    }

    uint64_t line = 0;
    sscanf(colon + 1, "%" PRIu64, &line);

    cz::String path = arg.slice_end(colon).clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(path.drop(cz::heap_allocator()));

    if (cz::file::exists(path.buffer())) {
        // Argument is of form FILE:LINE.
        open_file_tiling(editor, client, path, opened_count, line, 0);
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
        if (!cz::is_digit(colon[i])) {
            goto open;
        }
    }

    uint64_t column = 0;
    if (sscanf(colon + 1, "%" PRIu64, &column) < 1) {
        goto open;
    }
    std::swap(line, column);

    path.set_len(colon - path.buffer());
    path.null_terminate();

    if (cz::file::exists(path.buffer())) {
        // Argument is of form FILE:LINE:COLUMN.
        open_file_tiling(editor, client, path, opened_count, line, column);
        return;
    }

    goto open;
}

static void switch_to_the_home_directory() {
    ZoneScoped;

    // Go home only we are in the program directory or we can't identify ourselves.
    cz::String wd = {};
    CZ_DEFER(wd.drop(cz::heap_allocator()));
    if (cz::get_working_directory(cz::heap_allocator(), &wd).is_ok()) {
        cz::Str prog_dir = program_dir;
        // Get rid of the trailing / as it isn't in the working directory.
        prog_dir.len--;
        if (prog_dir != wd) {
            return;
        }
    }

    const char* user_home_path;
#ifdef _WIN32
    user_home_path = getenv("USERPROFILE");
#else
    user_home_path = getenv("HOME");
#endif

    if (user_home_path) {
        cz::set_working_directory(user_home_path);
    }
}

int mag_main(int argc, char** argv) {
    tracy::SetThreadName("Main thread");
    ZoneScoped;

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
            DWORD count = GetModuleFileNameA(NULL, program_name_storage.buffer(),
                                             (DWORD)program_name_storage.cap());
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

        cz::String program_dir_ = cz::Str(program_name).clone(cz::heap_allocator());
        CZ_DEFER(program_dir_.drop(cz::heap_allocator()));
        cz::path::convert_to_forward_slashes(&program_dir_);
        if (cz::path::pop_name(&program_dir_)) {
            program_dir_.null_terminate();
            program_dir = program_dir_.buffer();
        } else {
            program_dir = ".";
        }

        Server server = {};
        server.init();
        CZ_DEFER(server.drop());

        Buffer scratch = {};
        scratch.type = Buffer::TEMPORARY;
        scratch.name = cz::Str("*scratch*").clone(cz::heap_allocator());
        server.editor.create_buffer(scratch);

        Buffer splash_page = {};
        splash_page.type = Buffer::TEMPORARY;
        splash_page.name = cz::Str("*splash page*").clone(cz::heap_allocator());
        splash_page.read_only = true;
        splash_page.contents.append(
            "\
MMM             MMM\n\
MMMM           MMMM\n\
MM MM         MM MM\n\
MM  MM       MM  MM\n\
MM  MM.     .MM  MM      .aaaaa.      .gggggg.\n\
MM   MM     MM   MM    aa^     aa    ggg    ggg\n\
MM    MMM MMM    MM   aa       aa    gg      gg\n\
MM     MMMMM     MM   aa       aa    gg      gg\n\
MM     ^MMM^     MM    aaa     aa    ggg.   ggg\n\
MM      ^M^      MM      ^aaaa^ ^a    ^gggggggg\n\
                                             gg\n\
Mag: A customizable text editor.             gg\n\
                                             gg\n\
     ggg.             .g.          .g.     ggg\n\
         ^ggggggggggg^   ^gggggggg^   ^gggg^\n\
\n\
\n\
Mag is a WSIWYG editor -- typing text inserts text in the buffer, using Control or Alt modifiers\n\
runs commands.  Control is written as C-* and Alt is written as A-*.  Mag is moderately similar\n\
to Emacs but prefers Alt as a prefix over Control as it is easier to type on modern keyboards.\n\
\n\
Some basic key bindings are listed below.  Press F1 to view all key bindings.\n\
Configuration should be done by editing `custom/config.cpp`.\n\
\n\
\n\
Main key bindings:\n\
C-o     Open file            C-g     Stop action (cancel prompt, delete cursors, stop selecting region)\n\
C-s     Save file            A-LEFT  Backward navigation    (Use A-RIGHT to go forward)\n\
\n\
A-o     Go to other window   A-x b   Switch to open buffer\n\
A-x 3   Split vertically     A-x 2   Split horizontally\n\
A-/     Undo                 C-/     Redo\n\
\n\
C-SPACE Select region        A-w     Copy region\n\
A-y     Paste                C-y     Paste previous (used after A-y or C-y in tandem)\n\
\n\
C-f     Forward char         C-b     Backward char\n\
A-f     Forward word         A-b     Backward word\n\
C-A-f   Forward token        C-A-b   Backward token\n\
A-n     Forward line         A-p     Backward line          (Use C-A-* to create a cursor)\n\
A-q     Match token forward  A-j     Match token backward   (Use C-* to create a cursor)\n\
A-r     Search forward       C-r     Search backwards\n\
\n\
\n\
More advanced key bindings are found by using a prefix:\n\
A-x     Editor state commands\n\
A-c     Cursor commands\n\
A-g     Project or directory commands\n\
");
        server.editor.create_buffer(splash_page);

        custom::editor_created_callback(&server.editor);

        Client client = server.make_client();
        CZ_DEFER(client.drop());

        server.setup_async_context(&client);

        // 2021-06-06: the SDL window opens with this size so we just encode that now.
        // This prevents `open_arg` from panicking when it tries to split the window
        // and allows opening a file at a specific line to center it moderately well.
        client.window->set_size(38, 88);

        uint32_t opened_count = 0;
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
                open_arg(&server.editor, &client, arg, &opened_count);
                server.slurp_jobs();
            }
        }

        switch_to_the_home_directory();

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
