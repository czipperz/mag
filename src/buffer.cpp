#include "buffer.hpp"

#include <cz/bit_array.hpp>
#include <cz/heap.hpp>
#include "config.hpp"

namespace mag {

void Buffer::init(cz::Str path) {
    this->path = path.duplicate_null_terminate(cz::heap_allocator());

    mode = get_mode(path);
}

void Buffer::drop() {
    path.drop(cz::heap_allocator());
    commits.drop(cz::heap_allocator());

    unsigned char* dropped =
        (unsigned char*)calloc(1, cz::bit_array::alloc_size(_commit_id_counter));
    for (size_t i = 0; i < changes.len(); ++i) {
        if (!cz::bit_array::get(dropped, changes[i].commit.id.value)) {
            changes[i].commit.drop();
            cz::bit_array::set(dropped, changes[i].commit.id.value);
        }
    }
    free(dropped);
    changes.drop(cz::heap_allocator());

    contents.drop();
}

static void insert(Contents* contents, uint64_t position, cz::Str str) {
    CZ_ASSERT(position <= contents->len);
    contents->insert(position, str);
}

static void remove(Contents* contents, uint64_t position, uint64_t len) {
    CZ_ASSERT(position + len <= contents->len);
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

    // TODO: do position_after_edits for point and mark
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

    // TODO: do position_before_edits for point and mark
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
