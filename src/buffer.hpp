#pragma once

#include <cz/buffer_array.hpp>
#include <cz/option.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "change.hpp"
#include "commit.hpp"
#include "contents.hpp"
#include "cursor.hpp"
#include "mode.hpp"

namespace mag {

struct Buffer {
    cz::String path;

    cz::Vector<Commit> commits;
    size_t commit_index;

    cz::Vector<Change> changes;

    uint64_t _commit_id_counter;

    /// The last saved commit.  If no commits have been made (ie a file is
    /// opened), this is none, showing that no changes have been made.
    cz::Option<Commit_Id> saved_commit_id;

    Contents contents;

    cz::BufferArray copy_buffer;
    cz::Vector<Cursor> cursors;
    bool show_marks;

    Mode mode;

    void init(cz::Str path);

    void drop();

    bool undo();
    bool redo();

    /// Add this commit and apply it
    void commit(Commit commit);

    Commit_Id generate_commit_id() { return {_commit_id_counter++}; }

    cz::Option<Commit_Id> current_commit_id() const;

    bool is_unchanged() const;
    void mark_saved();
};

}
