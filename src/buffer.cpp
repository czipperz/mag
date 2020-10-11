#include "buffer.hpp"

#include <stdio.h>
#include <Tracy.hpp>
#include <cz/bit_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "config.hpp"
#include "diff.hpp"
#include "transaction.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace mag {

static bool get_file_time(const char* path, void* file_time) {
#ifdef _WIN32
    HANDLE handle = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (handle != INVALID_HANDLE_VALUE) {
        CZ_DEFER(CloseHandle(handle));
        if (GetFileTime(handle, NULL, NULL, (FILETIME*)file_time)) {
            return true;
        }
    }
    return false;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    *(time_t*)file_time = st.st_mtime;
    return true;
#endif
}

static bool is_out_of_date(const char* path, void* file_time) {
#ifdef _WIN32
    FILETIME new_ft;
#else
    time_t new_ft;
#endif

    if (!get_file_time(path, &new_ft)) {
        return false;
    }

#ifdef _WIN32
    if (CompareFileTime((FILETIME*)file_time, &new_ft) < 0) {
        *(FILETIME*)file_time = new_ft;
        return true;
    }
#else
    if (*(time_t*)file_time < new_ft) {
        *(time_t*)file_time = new_ft;
        return true;
    }
#endif

    return false;
}

static void initialize_file_time(Buffer* buffer) {
#ifdef _WIN32
    void* file_time = malloc(sizeof(FILETIME));
    CZ_ASSERT(file_time);
#else
    void* file_time = malloc(sizeof(time_t));
    CZ_ASSERT(file_time);
#endif
    if (get_file_time(buffer->path.buffer(), file_time)) {
        buffer->file_time = file_time;
    } else {
        free(file_time);
        buffer->file_time = nullptr;
    }
}

void Buffer::init(cz::Str path) {
    this->path = path.duplicate_null_terminate(cz::heap_allocator());

    mode = custom::get_mode(path);

    initialize_file_time(this);
}

void Buffer::check_for_external_update(Client* client) {
    if (!is_unchanged() || !file_time) {
        return;
    }

    if (is_out_of_date(path.buffer(), file_time)) {
        reload_file(client, this);
    }
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

    token_cache.drop();
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
        if (edit->flags & Edit::INSERT_MASK) {
            insert(&buffer->contents, edit->position, edit->value.as_str());
        } else {
            remove(&buffer->contents, edit->position, edit->value.len());
        }
    }
}

static void unapply_edits(Buffer* buffer, cz::Slice<const Edit> edits) {
    for (size_t i = edits.len; i-- > 0;) {
        const Edit* edit = &edits[i];
        if (edit->flags & Edit::INSERT_MASK) {
            remove(&buffer->contents, edit->position, edit->value.len());
        } else {
            insert(&buffer->contents, edit->position, edit->value.as_str());
        }
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

}
