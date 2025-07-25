#pragma once

#include <cz/buffer_array.hpp>
#include <cz/date.hpp>
#include <cz/option.hpp>
#include <cz/string.hpp>
#include <cz/vector.hpp>
#include "core/buffer_id.hpp"
#include "core/change.hpp"
#include "core/command.hpp"
#include "core/commit.hpp"
#include "core/contents.hpp"
#include "core/mode.hpp"
#include "core/ssostr.hpp"
#include "core/token_cache.hpp"

namespace mag {
struct Client;
struct Cursor;

struct Buffer {
    Buffer_Id id;

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
    enum Type {
        FILE,
        DIRECTORY,
        TEMPORARY,
    } type = FILE;

    bool has_file_time;
    cz::File_Time file_time;

    /// If `true` then the buffer should be saved with carriage returns.
    ///
    /// This is populated when a file is loaded from disk based on if the file contained carriage
    /// returns.  The `contents` will never have carriage returns.
    bool use_carriage_returns;

    /// If `true`, then `Commit`s will fail to be applied to this buffer.
    ///
    /// This is useful for buffers that do not represent files (ex. directories).
    /// Note that just because a `Buffer` is `TEMPORARY` doesn't mean it is `read_only`.
    /// For example the scratch and mini buffer are `TEMPORARY` buffers but not `read_only`.
    bool read_only;

    /// Storage for commit data.
    cz::Buffer_Array commit_buffer_array;

    /// All commits being tracked right now.  Note that those after `commit_index` have been undone.
    cz::Vector<Commit> commits;

    /// The index at which the next commit would be inserted into the `commits` list.
    ///
    /// Commits after `commit_index` but less than `commits.len()` have been undone.  When a new
    /// commit is added (via `Transaction::commit`), these commits will be erased.
    size_t commit_index;

    /// The last command to commit.
    ///
    /// This is useful for commands that want to merge consecutive calls into a single edit.  For
    /// example, see `command_insert_char` and `command_delete_backward_char`.
    ///
    /// When `undo` or `redo` are invoked, `last_committer` is reset to `null`.
    Command_Function last_committer;

    /// The list of all changes that have been applied to the `Buffer`.
    ///
    /// This is useful for tracking when the buffer changes: undos, redos, and commitss are all
    /// counted as `changes` but are difficult to track directly through the `commits` list.
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
    ///
    /// If the file is in read only mode, does nothing and returns `false`.
    /// Otherwise returns `true`.
    bool commit(cz::Slice<Edit> edits, Command_Function committer = nullptr);

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

    /// Composes the `directory` and `name` as well as adding a null terminator.
    ///
    /// If `type` is `TEMPORARY`, `get_path` returns `false` and not change the string.
    /// Otherwise, `get_path` will append `directory` and `name` to the `path` and return `true`.
    bool get_path(cz::Allocator allocator, cz::String* path) const;

    void render_name(cz::Allocator allocator, cz::String* string) const;

    void set_tokenizer(Tokenizer tokenizer);
};

SSOStr clear_buffer(Client* client, Buffer* buffer);

/// Creates a temp buffer named `*${temp_name}*` in the directory `dir`.
Buffer create_temp_buffer(cz::Str temp_name, cz::Option<cz::Str> dir = {});

}
