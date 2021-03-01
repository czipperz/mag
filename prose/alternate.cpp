#include "alternate.hpp"

#include <cz/file.hpp>
#include <cz/process.hpp>
#include "command_macros.hpp"
#include "file.hpp"

namespace mag {
namespace prose {

static bool replace_if_contains(cz::String& path, cz::Str search, cz::Str replacement) {
    const char* position = path.find(search);
    if (!position) {
        return false;
    }

    size_t index = position - path.buffer();
    path.reserve_total(cz::heap_allocator(), path.len() - search.len + replacement.len);
    path.remove(index, search.len);
    path.insert(index, replacement);
    return true;
}

static bool test_all_files(cz::String& path, cz::Slice<cz::Str> dest_extensions) {
    if (!replace_if_contains(path, "/cz/include/cz/", "/cz/src/")) {
        replace_if_contains(path, "/cz/src/", "/cz/include/cz/");
    }

    path.reserve(cz::heap_allocator(), 5);
    for (size_t i = 0; i < dest_extensions.len; ++i) {
        path.append(dest_extensions[i]);
        path.null_terminate();

        if (cz::file::does_file_exist(path.buffer())) {
            return true;
        }

        path.set_len(path.len() - dest_extensions[i].len);
    }

    return false;
}

static int test_extensions(cz::String& path,
                           cz::Slice<cz::Str> src_extensions,
                           cz::Slice<cz::Str> dest_extensions) {
    for (size_t i = 0; i < src_extensions.len; ++i) {
        if (path.ends_with(src_extensions[i])) {
            path.set_len(path.len() - src_extensions[i].len);
            if (test_all_files(path, dest_extensions)) {
                return 2;
            }
            return 1;
        }
    }
    return 0;
}

void command_alternate(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_SELECTED_BUFFER(source.client);

        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            return;
        }
    }

    cz::Str source_extensions[] = {".c", ".cc", ".cxx", ".cpp"};
    cz::Str header_extensions[] = {".h", ".hh", ".hxx", ".hpp"};

    int result = test_extensions(path, source_extensions, header_extensions);
    if (result == 0) {
        result = test_extensions(path, header_extensions, source_extensions);
    }

    if (result == 0) {
        source.client->show_message("File doesn't have a supported extension");
    } else if (result == 1) {
        source.client->show_message("Couldn't find the alternate file");
    } else if (result == 2) {
        open_file(editor, source.client, path);
    }
}

}
}
