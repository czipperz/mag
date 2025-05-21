#include "alternate.hpp"

#include <cz/file.hpp>
#include "core/command_macros.hpp"
#include "core/file.hpp"

namespace mag {
namespace prose {

static bool replace_if_contains(cz::String* path, cz::Str search, cz::Str replacement) {
    const char* position = path->find(search);
    if (!position) {
        return false;
    }

    size_t index = position - path->buffer;
    path->reserve_total(cz::heap_allocator(), path->len - search.len + replacement.len);
    path->remove_many(index, search.len);
    path->insert(index, replacement);
    return true;
}

static bool try_to_find_exact_match_with_new_extension(cz::String* path,
                                                       cz::Str src_extension,
                                                       cz::Slice<cz::Str> dest_extensions) {
    // Take off the source extension.
    path->len = path->len - src_extension.len;

    for (size_t i = 0; i < dest_extensions.len; ++i) {
        // Put on dest extension.
        path->reserve(cz::heap_allocator(), dest_extensions[i].len + 1);
        path->append(dest_extensions[i]);
        path->null_terminate();

        // If dest file exists, then we've found an exact match.
        if (cz::file::exists(path->buffer)) {
            return true;
        }

        // Remove dest extension.
        path->len = path->len - dest_extensions[i].len;
    }

    // Put back on source extension & return false to fail.
    path->append(src_extension);
    return false;
}

static int test_extensions(cz::String* path,
                           cz::Slice<cz::Str> src_extensions,
                           cz::Slice<cz::Str> dest_extensions) {
    bool any_match = false;
    size_t match;

    for (size_t i = 0; i < src_extensions.len; ++i) {
        // Only consider extension pairs that match the input path.
        if (!path->ends_with(src_extensions[i]))
            continue;

        // See if we can find a file on disk that uses a dest extension.
        if (try_to_find_exact_match_with_new_extension(path, src_extensions[i], dest_extensions)) {
            return 2;
        }

        // We can't find an exact match; maybe another extension yields an actual file.
        if (any_match) {
            if (src_extensions[i].len > src_extensions[match].len) {
                match = i;
            }
        } else {
            any_match = true;
            match = i;
        }
    }

    // No files were found so guess at the extension based on the first match.
    if (any_match) {
        path->len = path->len - src_extensions[match].len;
        path->append(dest_extensions[match]);
        return 1;
    }

    return 0;
}

int find_alternate_file(cz::String* path) {
    // Apply all `alternate_path` rules.
    for (size_t i = 0; i < alternate_path_len; ++i) {
        if (!replace_if_contains(path, alternate_path_1[i], alternate_path_2[i]))
            replace_if_contains(path, alternate_path_2[i], alternate_path_1[i]);
    }

    cz::Slice<cz::Str> extensions_1 = {alternate_extensions_1, alternate_extensions_len};
    cz::Slice<cz::Str> extensions_2 = {alternate_extensions_2, alternate_extensions_len};

    int result = test_extensions(path, extensions_1, extensions_2);
    if (result == 0) {
        result = test_extensions(path, extensions_2, extensions_1);
    }
    return result;
}

REGISTER_COMMAND(command_alternate);
void command_alternate(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    {
        WITH_CONST_SELECTED_BUFFER(source.client);

        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            return;
        }
    }

    int result = find_alternate_file(&path);

    if (result == 0) {
        source.client->show_message("File doesn't have a supported extension");
        return;
    }

    if (result == 1) {
        source.client->show_message("Couldn't find the alternate file; guessing on the extension");
    }

    open_file(editor, source.client, path);
}

}
}
