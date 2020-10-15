#pragma once

#include <cz/buffer_array.hpp>
#include <cz/option.hpp>
#include <cz/string.hpp>
#include <cz/vector.hpp>
#include "buffer_id.hpp"
#include "change.hpp"
#include "command.hpp"
#include "commit.hpp"
#include "contents.hpp"
#include "mode.hpp"
#include "token_cache.hpp"

namespace mag {
struct Client;
struct Cursor;

struct Buffer {
    /// The directory the buffer is located in.
    ///
    /// Should be delineated by forward slashes (`'/'`).  Should be terminated by a forward slash
    /// and a null terminator (`'\0'`).
    ///
    /// If it doesn't make sense for this buffer to have an associated directory, leave this
    /// completely empty.
    cz::String directory;
    /// The name of the buffer.
    ///
    /// If this is a file, it should be the final component only (no slashes).
    /// If this is a directory, it is `"."`.
    /// If this is a temporary buffer, it can be an arbitrary string.
    cz::String name;
    enum {
        FILE,
        DIRECTORY,
        TEMPORARY,
    } type = FILE;

    void* file_time;

    cz::Vector<Commit> commits;
    size_t commit_index;

    /// The last command to commit.
    ///
    /// This is useful for commands that want to merge consecutive calls into a single edit.  For
    /// example, see `command_insert_char` and `command_delete_backward_char`.
    ///
    /// When `undo` or `redo` are invoked, `last_committer` is reset to `null`.
    Command_Function last_committer;

    cz::Vector<Change> changes;

    uint64_t _commit_id_counter;

    /// The last saved commit.  If no commits have been made (ie a file is
    /// opened), this is none, showing that no changes have been made.
    cz::Option<Commit_Id> saved_commit_id;

    Contents contents;

    Mode mode;

    Token_Cache token_cache;

    void init();

    void drop();

    bool undo();
    bool redo();

    /// Add this commit and apply it.
    ///
    /// You can optionally specify the committer to set the `last_comitter` field.  See
    /// `last_committer` for more details.
    void commit(Commit commit, Command_Function committer = nullptr);

    /// Checks if the last committer is the same as `committer` and if the last commit's edits were
    /// at the same positions as the cursors.
    ///
    /// This is useful for commands that want to merge consecutive calls into a single edit.  For
    /// example, see `command_insert_char` and `command_delete_backward_char`.
    bool check_last_committer(Command_Function committer, cz::Slice<Cursor> cursors) const;

    Commit_Id generate_commit_id() { return {_commit_id_counter++}; }

    cz::Option<Commit_Id> current_commit_id() const;

    bool is_unchanged() const;
    void mark_saved();

    void check_for_external_update(Client* client);

    /// Composes the `directory` and `name` as well as adding a null terminator.
    ///
    /// If `type` is `TEMPORARY`, `get_path` returns `false` and not change the string.
    /// Otherwise, `get_path` will append `directory` and `name` to the `path` and return `true`.
    bool get_path(cz::Allocator allocator, cz::String* path) const;

    void render_name(cz::Allocator allocator, cz::String* string) const;
};

cz::Str clear_buffer(Buffer* buffer);

}
