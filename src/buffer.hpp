#pragma once

#include <cz/buffer_array.hpp>
#include <cz/option.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "commit.hpp"
#include "contents.hpp"
#include "cursor.hpp"
#include "mode.hpp"

namespace mag {

struct Buffer {
    Buffer_Id id;
    cz::String name;
    cz::String directory;

    cz::BufferArray edit_buffer;
    cz::BufferArray commit_buffer;
    cz::Vector<Commit> commits;
    size_t commit_index;

    Contents contents;

    cz::BufferArray copy_buffer;
    cz::Vector<Cursor> cursors;
    bool show_marks;

    Mode mode;

    void init(Buffer_Id id, cz::Str name, cz::Option<cz::Str> directory);

    void drop();

    bool undo();
    bool redo();

    /// Add this commit and apply it
    void commit(Commit commit);
};

}
