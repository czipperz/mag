#include "alternate.hpp"

#include "command_macros.hpp"
#include "file.hpp"

namespace mag {
namespace prose {

void command_alternate(Editor* editor, Command_Source source) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    WITH_SELECTED_BUFFER({
        if (!buffer->path.ends_with(".cpp") && !buffer->path.ends_with(".hpp")) {
            return;
        }
        path = buffer->path.clone(cz::heap_allocator());
    });

    if (path.ends_with(".cpp")) {
        path[path.len() - 3] = 'h';
    } else if (path.ends_with(".hpp")) {
        path[path.len() - 3] = 'c';
    }

    open_file(editor, source.client, path);
}

}
}
