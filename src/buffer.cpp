#include "buffer.hpp"

#include <cz/heap.hpp>
#include "config.hpp"

namespace mag {

void Buffer::init(Buffer_Id id, cz::Str path) {
    this->id = id;
    this->path = path.duplicate(cz::heap_allocator());

    copy_buffer.create();
    cursors.reserve(cz::heap_allocator(), 1);
    cursors.push({});

    mode = get_mode(path);
}

void Buffer::drop() {
    path.drop(cz::heap_allocator());
    for (size_t commit = 0; commit < commits.len(); ++commit) {
        commits[commit].drop();
    }
    commits.drop(cz::heap_allocator());
    contents.drop();
    copy_buffer.drop();
    cursors.drop(cz::heap_allocator());
}

static void fix_point_insert(uint64_t position, uint64_t len, uint64_t* point) {
    if (*point >= position) {
        *point += len;
    }
}

static void insert(Buffer* buffer, uint64_t position, cz::Str str) {
    CZ_ASSERT(position <= buffer->contents.len());
    buffer->contents.insert(position, str);

    for (size_t i = 0; i < buffer->cursors.len(); ++i) {
        fix_point_insert(position, str.len, &buffer->cursors[i].point);
        fix_point_insert(position, str.len, &buffer->cursors[i].mark);
    }
}

static void fix_point_remove(uint64_t position, uint64_t len, uint64_t* point) {
    if (*point >= position) {
        if (*point >= position + len) {
            *point -= len;
        } else {
            *point = position;
        }
    }
}

static void remove(Buffer* buffer, uint64_t position, uint64_t len) {
    CZ_ASSERT(position + len <= buffer->contents.len());
    buffer->contents.remove(position, len);

    for (size_t i = 0; i < buffer->cursors.len(); ++i) {
        fix_point_remove(position, len, &buffer->cursors[i].point);
        fix_point_remove(position, len, &buffer->cursors[i].mark);
    }
}

static void apply_commit(Buffer* buffer, Commit* commit) {
    for (size_t i = 0; i < commit->edits.len; ++i) {
        Edit* edit = &commit->edits[i];
        if (edit->is_insert) {
            insert(buffer, edit->position, edit->value.as_str());
        } else {
            remove(buffer, edit->position, edit->value.len());
        }
    }
}

static void unapply_commit(Buffer* buffer, Commit* commit) {
    for (size_t i = commit->edits.len; i-- > 0;) {
        Edit* edit = &commit->edits[i];
        if (edit->is_insert) {
            remove(buffer, edit->position, edit->value.len());
        } else {
            insert(buffer, edit->position, edit->value.as_str());
        }
    }
}

bool Buffer::undo() {
    if (commit_index == 0) {
        return false;
    }

    --commit_index;
    unapply_commit(this, &commits[commit_index]);
    return true;
}

bool Buffer::redo() {
    if (commit_index == commits.len()) {
        return false;
    }

    apply_commit(this, &commits[commit_index]);
    ++commit_index;
    return true;
}

void Buffer::commit(Commit commit) {
    commits.set_len(commit_index);
    commits.reserve(cz::heap_allocator(), 1);
    commits.push(commit);
    redo();
}

}
