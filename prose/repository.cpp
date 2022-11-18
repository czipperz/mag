#include "repository.hpp"

#include <cz/format.hpp>
#include <cz/process.hpp>
#include "command_macros.hpp"
#include "movement.hpp"
#include "process.hpp"
#include "prose/helpers.hpp"

namespace mag {
namespace prose {

void command_open_file_on_repo_site(Editor* editor, Command_Source source) {
    cz::String buffer_path = {};
    CZ_DEFER(buffer_path.drop(cz::heap_allocator()));
    cz::String vc_dir = {};
    CZ_DEFER(vc_dir.drop(cz::heap_allocator()));
    uint64_t start = 0, end = 0;
    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        if (!buffer->get_path(cz::heap_allocator(), &buffer_path)) {
            source.client->show_message("Error: buffer is not a file");
            return;
        }

        if (!copy_version_control_directory(source.client, buffer, &vc_dir))
            return;

        if (window->show_marks) {
            auto start_it = buffer->contents.iterator_at(window->sel().start());
            auto end_it = buffer->contents.iterator_at(window->sel().end());
            if (at_start_of_line(end_it) && end_it.position > start_it.position)
                backward_char(&end_it);
            start = start_it.get_line_number() + 1;
            end = end_it.get_line_number() + 1;
        } else {
            start = end = buffer->contents.get_line_number(window->sel().point) + 1;
        }
    }

    ///////////////////////////////////////////////////////
    // Get repository origin
    ///////////////////////////////////////////////////////

    cz::String origin = {};
    CZ_DEFER(origin.drop(cz::heap_allocator()));

    cz::Str git_args[] = {"git", "config", "remote.origin.url"};
    if (!run_process_for_output(source.client, git_args, "git", vc_dir.buffer,
                                cz::heap_allocator(), &origin))
        return;

    if (origin.ends_with('\n'))
        origin.len--;

    // git@github.com:czipperz/mag -> https://github.com/czipperz/mag
    if (origin.starts_with("git@")) {
        origin.remove_range(0, strlen("git@"));
        if (auto colon = origin.find(':')) {
            *colon = '/';
        }
        cz::Str to_insert = "https://";
        origin.reserve_exact(cz::heap_allocator(), to_insert.len + 1);
        origin.insert(0, to_insert);
    }

    if (origin.ends_with(".git"))
        origin.len -= strlen(".git");

    origin.null_terminate();

    ///////////////////////////////////////////////////////
    // Launch browser
    ///////////////////////////////////////////////////////

    cz::Str relpath = buffer_path.slice_start(vc_dir.len);
    cz::Heap_String url = cz::format(origin, "/blob/master/", relpath);
    CZ_DEFER(url.drop());
    cz::append(&url, "#L", start);
    if (start != end)
        cz::append(&url, "-L", end);

#ifdef _WIN32
    cz::Str browser_args[] = {"C:/Program Files (x86)/Google/Chrome/Application/chrome.exe", url};
    // cz::Str browser_args[] = {"C:/Program Files/Mozilla Firefox/firefox.exe", url};
#elif defined(__APPLE__)
    cz::Str browser_args[] = {"open", "-a", "/Applications/Google Chrome.app", url};
    // cz::Str browser_args[] = {"open", "-a", "/Applications/Mozilla Firefox.app", url};
#else
    cz::Str browser_args[] = {"google-chrome", url};
    // cz::Str browser_args[] = {"firefox", url};
#endif

    cz::Process browser;
    cz::Process_Options browser_options;
    if (!browser.launch_program(browser_args, browser_options)) {
        source.client->show_message("Error: couldn't launch browser");
        return;
    }
    browser.detach();

    cz::String message = cz::format("Opening ", url);
    CZ_DEFER(message.drop(cz::heap_allocator()));
    source.client->show_message(message);
}

}
}
