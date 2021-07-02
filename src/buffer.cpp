#include "buffer.hpp"

#include <stdio.h>
#include <Tracy.hpp>
#include <cz/bit_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "cursor.hpp"
#include "diff.hpp"
#include "file.hpp"
#include "match.hpp"
#include "transaction.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace mag {

static void initialize_file_time(Buffer* buffer) {
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    if (!buffer->get_path(cz::heap_allocator(), &path)) {
        return;
    }

    buffer->has_file_time = get_file_time(path.buffer(), &buffer->file_time);
}

void Buffer::init() {
    initialize_file_time(this);
    commit_buffer_array.init();
}

void Buffer::drop() {
    directory.drop(cz::heap_allocator());
    name.drop(cz::heap_allocator());

    commit_buffer_array.drop();
    commits.drop(cz::heap_allocator());
    changes.drop(cz::heap_allocator());

    contents.drop();

    token_cache.drop();

    mode.drop();
}

static bool insert(Contents* contents, uint64_t position, cz::Str str) {
    if (position > contents->len) {
        return false;
    }

    contents->insert(position, str);
    return true;
}

static bool remove(Contents* contents, uint64_t position, cz::Str str) {
    if (!looking_at(contents->iterator_at(position), str)) {
        return false;
    }

    contents->remove(position, str.len);
    return true;
}

static bool unapply_edits(Buffer* buffer, cz::Slice<const Edit> edits);
static bool apply_edits(Buffer* buffer, cz::Slice<const Edit> edits) {
    for (size_t i = 0; i < edits.len; ++i) {
        bool r;

        const Edit* edit = &edits[i];
        if (edit->flags & Edit::INSERT_MASK) {
            r = insert(&buffer->contents, edit->position, edit->value.as_str());
        } else {
            r = remove(&buffer->contents, edit->position, edit->value.as_str());
        }

        if (!r) {
            // Undo all changes because an edit failed to apply.
            unapply_edits(buffer, edits.slice_end(i));
            return false;
        }
    }
    return true;
}

static bool unapply_edits(Buffer* buffer, cz::Slice<const Edit> edits) {
    for (size_t i = edits.len; i-- > 0;) {
        bool r;

        const Edit* edit = &edits[i];
        if (edit->flags & Edit::INSERT_MASK) {
            r = remove(&buffer->contents, edit->position, edit->value.as_str());
        } else {
            r = insert(&buffer->contents, edit->position, edit->value.as_str());
        }

        if (!r) {
            // Undo all changes because an edit failed to apply.
            apply_edits(buffer, edits.slice_start(i + 1));
            return false;
        }
    }
    return true;
}

bool Buffer::undo() {
    ZoneScoped;

    if (read_only || commit_index == 0) {
        return false;
    }

    Commit commit = commits[commit_index - 1];

    // If the edit doesn't apply then someone edited the buffer manually.  We should
    // just force the edit to fail so we don't get into a corrupted state.
    if (!unapply_edits(this, commit.edits)) {
        return false;
    }

    // Track the change to the buffer.
    Change change;
    change.commit = commit;
    change.is_redo = false;
    changes.reserve(cz::heap_allocator(), 1);
    changes.push(change);

    --commit_index;

    last_committer = nullptr;

    return true;
}

bool Buffer::redo() {
    ZoneScoped;

    if (read_only || commit_index == commits.len()) {
        return false;
    }

    Commit commit = commits[commit_index];

    // If the edit doesn't apply then someone edited the buffer manually.  We should
    // just force the edit to fail so we don't get into a corrupted state.
    if (!apply_edits(this, commit.edits)) {
        return false;
    }

    // Track the change to the buffer.
    Change change;
    change.commit = commit;
    change.is_redo = true;
    changes.reserve(cz::heap_allocator(), 1);
    changes.push(change);

    ++commit_index;

    last_committer = nullptr;

    return true;
}

bool Buffer::commit(Commit commit, Command_Function committer) {
    ZoneScoped;

    if (read_only) {
        return false;
    }

    // If the edit doesn't apply then the creator messed up; in this case we
    // shouldn't commit the edits because they will corrupt the undo tree.
    if (!apply_edits(this, commit.edits)) {
        return false;
    }

    // Push the commit onto the undo tree.
    commits.set_len(commit_index);
    commit.id = generate_commit_id();
    commits.reserve(cz::heap_allocator(), 1);
    commits.push(commit);

    // Track the change to the buffer.
    Change change;
    change.commit = commits[commit_index];
    change.is_redo = true;
    changes.reserve(cz::heap_allocator(), 1);
    changes.push(change);
    ++commit_index;

    last_committer = committer;
    return true;
}

bool Buffer::check_last_committer(Command_Function committer, cz::Slice<Cursor> cursors) const {
    if (last_committer != committer) {
        return false;
    }

    cz::Slice<const Edit> edits = commits[commit_index - 1].edits;

    if (edits.len != cursors.len) {
        return false;
    }

    for (size_t i = 0; i < edits.len; ++i) {
        if (edits[i].flags == Edit::INSERT) {
            if (edits[i].position + edits[i].value.len() != cursors[i].point) {
                return false;
            }
        } else {
            if (edits[i].position != cursors[i].point) {
                return false;
            }
        }
    }

    return true;
}

cz::Option<Commit_Id> Buffer::current_commit_id() const {
    if (commit_index > 0) {
        return {commits[commit_index - 1].id};
    } else {
        return {};
    }
}

bool Buffer::is_unchanged() const {
    return current_commit_id() == saved_commit_id;
}

void Buffer::mark_saved() {
    if (commit_index == 0) {
        saved_commit_id = {};
    } else {
        saved_commit_id = {commits[commit_index - 1].id};
    }

    initialize_file_time(this);
}

SSOStr clear_buffer(Buffer* buffer) {
    if (buffer->contents.len == 0) {
        return {};
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Edit edit;
    edit.value = buffer->contents.slice(transaction.value_allocator(),
                                        buffer->contents.iterator_at(0), buffer->contents.len);
    edit.position = 0;
    edit.flags = Edit::REMOVE;
    transaction.push(edit);

    SSOStr buffer_contents = transaction.last_edit_value();

    transaction.commit();

    return buffer_contents;
}

bool Buffer::get_path(cz::Allocator allocator, cz::String* path) const {
    if (type == TEMPORARY) {
        return false;
    }

    path->reserve(allocator, directory.len() + name.len() + 1);
    path->append(directory);
    path->append(name);
    path->null_terminate();

    return true;
}

void Buffer::render_name(cz::Allocator allocator, cz::String* string) const {
    if (type == Buffer::TEMPORARY) {
        if (directory.len() == 0) {
            string->reserve(allocator, name.len());
            string->append(name);
        } else {
            string->reserve(allocator, name.len() + 3 + directory.len());
            string->append(name);
            string->append(" (");
            string->append(directory);
            string->push(')');
        }
    } else {
        get_path(allocator, string);
    }
}

}
