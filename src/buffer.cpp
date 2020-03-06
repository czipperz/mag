#include "buffer.hpp"

#include <cz/heap.hpp>
#include "config.hpp"

namespace mag {

void Buffer::init(Buffer_Id id, cz::Str path) {
    this->id = id;
    this->path = path.duplicate_null_terminate(cz::heap_allocator());

    copy_buffer.create();
    cursors.reserve(cz::heap_allocator(), 1);
    cursors.push({});

    mode = get_mode(path);
}

void Buffer::drop() {
    path.drop(cz::heap_allocator());
    commits.drop(cz::heap_allocator());

    for (size_t i = 0; i < changes.len(); ++i) {
        changes[i].commit.drop();
    }
    changes.drop(cz::heap_allocator());

    contents.drop();
    copy_buffer.drop();
    cursors.drop(cz::heap_allocator());
}

static void insert(Contents* contents, uint64_t position, cz::Str str) {
    CZ_ASSERT(position <= contents->len());
    contents->insert(position, str);
}

static void remove(Contents* contents, uint64_t position, uint64_t len) {
    CZ_ASSERT(position + len <= contents->len());
    contents->remove(position, len);
}

static void apply_edits(Buffer* buffer, cz::Slice<const Edit> edits) {
    for (size_t i = 0; i < edits.len; ++i) {
        const Edit* edit = &edits[i];
        if (edit->is_insert) {
            insert(&buffer->contents, edit->position, edit->value.as_str());
        } else {
            remove(&buffer->contents, edit->position, edit->value.len());
        }
    }

    for (size_t c = 0; c < buffer->cursors.len(); ++c) {
        position_after_edits(edits, &buffer->cursors[c].point);
        position_after_edits(edits, &buffer->cursors[c].mark);
    }
}

static void unapply_edits(Buffer* buffer, cz::Slice<const Edit> edits) {
    for (size_t i = edits.len; i-- > 0;) {
        const Edit* edit = &edits[i];
        if (edit->is_insert) {
            remove(&buffer->contents, edit->position, edit->value.len());
        } else {
            insert(&buffer->contents, edit->position, edit->value.as_str());
        }
    }

    for (size_t c = 0; c < buffer->cursors.len(); ++c) {
        position_before_edits(edits, &buffer->cursors[c].point);
        position_before_edits(edits, &buffer->cursors[c].mark);
    }
}

bool Buffer::undo() {
    if (commit_index == 0) {
        return false;
    }

    --commit_index;

    Change change;
    change.commit = commits[commit_index];
    change.is_redo = false;
    changes.reserve(cz::heap_allocator(), 1);
    changes.push(change);

    unapply_edits(this, change.commit.edits);

    return true;
}

bool Buffer::redo() {
    if (commit_index == commits.len()) {
        return false;
    }

    Change change;
    change.commit = commits[commit_index];
    change.is_redo = true;
    changes.reserve(cz::heap_allocator(), 1);
    changes.push(change);

    apply_edits(this, change.commit.edits);

    ++commit_index;

    return true;
}

void Buffer::commit(Commit commit) {
    commits.set_len(commit_index);

    commit.id = generate_commit_id();
    commits.reserve(cz::heap_allocator(), 1);
    commits.push(commit);

    redo();
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
}

}
