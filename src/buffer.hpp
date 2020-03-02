#pragma once

#include <cz/buffer_array.hpp>
#include <cz/heap.hpp>
#include <cz/string.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "commit.hpp"
#include "contents.hpp"
#include "cursor.hpp"

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

    void init(Buffer_Id id, cz::Str name) {
        this->id = id;
        this->name = name.duplicate(cz::heap_allocator());

        edit_buffer.create();
        commit_buffer.create();

        cursors.reserve(cz::heap_allocator(), 1);
        cursors.push({});
    }

    void drop() {
        name.drop(cz::heap_allocator());
        edit_buffer.drop();
        commit_buffer.drop();
        commits.drop(cz::heap_allocator());
        contents.drop();
        cursors.drop(cz::heap_allocator());
    }

    bool undo();
    bool redo();

    /// Add this commit and apply it
    void commit(Commit commit);
};

}
