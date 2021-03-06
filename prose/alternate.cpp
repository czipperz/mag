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
    path.remove_many(index, search.len);
    path.insert(index, replacement);
    return true;
}

static bool test_all_files(cz::String& path, cz::Slice<cz::Str> dest_extensions) {
    path.reserve(cz::heap_allocator(), 5);
    for (size_t i = 0; i < dest_extensions.len; ++i) {
        path.append(dest_extensions[i]);
        path.null_terminate();

        if (cz::file::exists(path.buffer())) {
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

            // See if we can find the paired file.
            if (test_all_files(path, dest_extensions)) {
                return 2;
            }

            // If not we want to approximate by filling in the paired extension.
            path.append(dest_extensions[i]);
            return 1;
        }
    }

    return 0;
}

void command_alternate(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            return;
        }
    }

    bool any_changes = false;

    // Apply all `alternate_path` rules.
    for (size_t i = 0; i < alternate_path_len; ++i) {
        if (replace_if_contains(path, alternate_path_1[i], alternate_path_2[i])) {
            any_changes = true;
        } else if (replace_if_contains(path, alternate_path_2[i], alternate_path_1[i])) {
            any_changes = true;
        }
    }

    cz::Slice<cz::Str> extensions_1 = {alternate_extensions_1, alternate_extensions_len};
    cz::Slice<cz::Str> extensions_2 = {alternate_extensions_2, alternate_extensions_len};

    int result = test_extensions(path, extensions_1, extensions_2);
    if (result == 0) {
        result = test_extensions(path, extensions_2, extensions_1);
    }

    if (!any_changes && result == 0) {
        source.client->show_message(editor, "File doesn't have a supported extension");
        return;
    }

    if (result == 1) {
        source.client->show_message(editor,
                                    "Couldn't find the alternate file; guessing on the extension");
    }

    open_file(editor, source.client, path);
}

}
}
