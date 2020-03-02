#pragma once

#include <cz/buffer_array.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "commit.hpp"
#include "contents.hpp"
#include "cursor.hpp"
#include "tokenizer.hpp"

namespace mag {

struct Buffer {
    Buffer_Id id;
    cz::String name;

    cz::BufferArray edit_buffer;
    cz::BufferArray commit_buffer;
    cz::Vector<Commit> commits;
    size_t commit_index;

    Contents contents;

    cz::Vector<Cursor> cursors;
    bool show_marks;

    Tokenizer tokenizer;

    void init(Buffer_Id id, cz::Str name);

    void drop();

    bool undo();
    bool redo();

    /// Add this commit and apply it
    void commit(Commit commit);
};

}
