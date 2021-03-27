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
}

void Buffer::drop() {
    directory.drop(cz::heap_allocator());
    name.drop(cz::heap_allocator());

    commits.drop(cz::heap_allocator());

    cz::Sized_Bit_Array dropped;
    dropped.init(cz::heap_allocator(), _commit_id_counter);
    CZ_DEFER(dropped.drop(cz::heap_allocator()));
    for (size_t i = 0; i < changes.len(); ++i) {
        if (!dropped.get(changes[i].commit.id.value)) {
            changes[i].commit.drop();
            dropped.set(changes[i].commit.id.value);
        }
    }

    changes.drop(cz::heap_allocator());

    contents.drop();

    token_cache.drop();
}

static void insert(Contents* contents, uint64_t position, cz::Str str) {
    CZ_ASSERT(position <= contents->len);
    contents->insert(position, str);
}

static void remove(Contents* contents, uint64_t position, cz::Str str) {
    CZ_ASSERT(position + str.len <= contents->len);
    CZ_DEBUG_ASSERT(looking_at(contents->iterator_at(position), str));
    contents->remove(position, str.len);
}

static void apply_edits(Buffer* buffer, cz::Slice<const Edit> edits) {
    for (size_t i = 0; i < edits.len; ++i) {
        const Edit* edit = &edits[i];
        if (edit->flags & Edit::INSERT_MASK) {
            insert(&buffer->contents, edit->position, edit->value.as_str());
        } else {
            remove(&buffer->contents, edit->position, edit->value.as_str());
        }
    }
}

static void unapply_edits(Buffer* buffer, cz::Slice<const Edit> edits) {
    for (size_t i = edits.len; i-- > 0;) {
        const Edit* edit = &edits[i];
        if (edit->flags & Edit::INSERT_MASK) {
            remove(&buffer->contents, edit->position, edit->value.as_str());
        } else {
            insert(&buffer->contents, edit->position, edit->value.as_str());
        }
    }
}

bool Buffer::undo() {
    if (read_only || commit_index == 0) {
        return false;
    }

    --commit_index;

    Change change;
    change.commit = commits[commit_index];
    change.is_redo = false;
    changes.reserve(cz::heap_allocator(), 1);
    changes.push(change);

    unapply_edits(this, change.commit.edits);

    last_committer = nullptr;

    return true;
}

bool Buffer::redo() {
    if (read_only || commit_index == commits.len()) {
        return false;
    }

    Change change;
    change.commit = commits[commit_index];
    change.is_redo = true;
    changes.reserve(cz::heap_allocator(), 1);
    changes.push(change);

    apply_edits(this, change.commit.edits);

    ++commit_index;

    last_committer = nullptr;

    return true;
}

bool Buffer::commit(Commit commit, Command_Function committer) {
    if (read_only) {
        return false;
    }

    commits.set_len(commit_index);

    commit.id = generate_commit_id();
    commits.reserve(cz::heap_allocator(), 1);
    commits.push(commit);

    redo();

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

cz::Str clear_buffer(Buffer* buffer) {
    if (buffer->contents.len == 0) {
        return {};
    }

    Transaction transaction;
    transaction.init(1, (size_t)buffer->contents.len);
    CZ_DEFER(transaction.drop());

    Edit edit;
    edit.value = buffer->contents.slice(transaction.value_allocator(),
                                        buffer->contents.iterator_at(0), buffer->contents.len);
    edit.position = 0;
    edit.flags = Edit::REMOVE;
    transaction.push(edit);

    cz::Str buffer_contents = transaction.last_edit_value();

    transaction.commit(buffer);

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
