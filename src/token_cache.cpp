#include "token_cache.hpp"

#include <stdlib.h>
#include <Tracy.hpp>
#include <cz/bit_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include "buffer.hpp"
#include "buffer_handle.hpp"
#include "command_macros.hpp"
#include "contents.hpp"
#include "job.hpp"
#include "token.hpp"
#include "tracy_format.hpp"

namespace mag {

void Token_Cache::drop() {
    check_points.drop(cz::heap_allocator());
}

Token_Cache Token_Cache::clone() const {
    Token_Cache cache = *this;
    cache.check_points = cache.check_points.clone(cz::heap_allocator());
    return cache;
}

void Token_Cache::reset() {
    change_index = 0;
    check_points.set_len(0);
    ran_to_end = false;
}

bool Token_Cache::find_check_point(uint64_t position, Tokenizer_Check_Point* cp) const {
    size_t result_index;
    if (find_check_point(position, &result_index)) {
        *cp = check_points[result_index];
        return true;
    } else {
        return false;
    }
}

bool Token_Cache::find_check_point(uint64_t position, size_t* index_out) const {
    ZoneScoped;

    size_t start = 0;
    size_t end = check_points.len();
    size_t result_index = check_points.len();
    while (start < end) {
        size_t mid = (start + end) / 2;
        if (check_points[mid].position == position) {
            result_index = mid;
            break;
        } else if (check_points[mid].position < position) {
            result_index = mid;
            start = mid + 1;
        } else {
            end = mid;
        }
    }

    if (result_index < check_points.len()) {
        *index_out = result_index;
        return true;
    } else {
        return false;
    }
}

static bool any_changes_after(cz::Slice<const Change> changes, uint64_t position) {
    for (size_t c = 0; c < changes.len; ++c) {
        if (changes[c].is_redo) {
            for (size_t e = 0; e < changes[c].commit.edits.len; ++e) {
                auto& edit = changes[c].commit.edits[e];
                if (edit.position >= position) {
                    return true;
                }
                position_after_edit(edit, &position);
            }
        } else {
            for (size_t e = changes[c].commit.edits.len; e-- > 0;) {
                auto& edit = changes[c].commit.edits[e];
                if (edit.position >= position) {
                    return true;
                }
                position_before_edit(edit, &position);
            }
        }
    }
    return false;
}

void Token_Cache::update(const Buffer* buffer) {
    ZoneScoped;

    cz::Slice<const Change> changes = buffer->changes;
    cz::Slice<const Change> pending_changes = {changes.elems + change_index,
                                               changes.len - change_index};
    cz::Sized_Bit_Array changed_check_points;
    changed_check_points.init(cz::heap_allocator(), check_points.len());
    CZ_DEFER(changed_check_points.drop(cz::heap_allocator()));

    // Detect check points that changed
    for (size_t i = 1; i < check_points.len(); ++i) {
        uint64_t pos = check_points[i].position;

        bool cntinue = any_changes_after(pending_changes, check_points[i].position);

        position_after_changes(pending_changes, &pos);

        if (check_points[i].position != pos) {
            changed_check_points.set(i);
        }

        uint64_t offset = pos - check_points[i].position;
        check_points[i].position = pos;

        if (!cntinue) {
            for (size_t j = i + 1; j < check_points.len(); ++j) {
                check_points[j].position += offset;
            }
            break;
        }
    }
    change_index = changes.len;

    Token token;
    token.end = 0;
    uint64_t state = 0;
    // Fix check points that were changed
    for (size_t i = 0; i < check_points.len(); ++i) {
        uint64_t end_position = check_points[i].position;
        if (changed_check_points.get(i)) {
            Contents_Iterator iterator = buffer->contents.iterator_at(token.end);
            size_t start = i;
            // Efficiently loop without recalculating the iterator so long as
            // the edit is screwing up future check points.
            while (i < check_points.len()) {
                ZoneScopedN("mag::Token_Cache::update: one check point");

                // If we don't resolve after 3 check points we probably won't in the immediate
                // future so just discard our results.  This prevents us from stalling a
                // really long time when the user starts typing a block comment at the start
                // of a big file.  If we're on the main thread this will automatically kick
                // off a background thread to tokenize the rest of the file.
                if (i == start + 3) {
                    TracyFormat(message, len, 1024, "Discarding check points after index %lu",
                                (unsigned long)i);
                    TracyMessage(message, len);
                    check_points.set_len(i);
                    return;
                }

                while (token.end < end_position) {
                    if (!buffer->mode.next_token(&iterator, &token, &state)) {
                        break;
                    }
                }

                if (token.end > end_position || state != check_points[i].state) {
                    check_points[i].position = token.end;
                    check_points[i].state = state;
                    ++i;
                    if (i == check_points.len()) {
                        return;
                    }
                    end_position = check_points[i].position;
                } else {
                    break;
                }
            }
        }

        token.end = check_points[i].position;
        state = check_points[i].state;
    }
}

void Token_Cache::generate_check_points_until(const Buffer* buffer, uint64_t position) {
    ZoneScoped;

    uint64_t state;
    Contents_Iterator iterator;
    if (check_points.len() > 0) {
        state = check_points.last().state;
        iterator = buffer->contents.iterator_at(check_points.last().position);
    } else {
        state = 0;
        iterator = buffer->contents.start();

        Tokenizer_Check_Point check_point;
        check_point.position = iterator.position;
        check_point.state = state;
        check_points.reserve(cz::heap_allocator(), 1);
        check_points.push(check_point);
    }

    while (iterator.position <= position) {
        if (!next_check_point(buffer, &iterator, &state)) {
            break;
        }
    }
}

bool Token_Cache::next_check_point(const Buffer* buffer,
                                   Contents_Iterator* iterator,
                                   uint64_t* state) {
    ZoneScoped;

    uint64_t start_position = iterator->position;
    while (!iterator->at_eob()) {
        if (iterator->position >= start_position + 1024) {
            Tokenizer_Check_Point check_point;
            check_point.position = iterator->position;
            check_point.state = *state;
            check_points.reserve(cz::heap_allocator(), 1);
            check_points.push(check_point);
            return true;
        }

        Token token;
        if (!buffer->mode.next_token(iterator, &token, state)) {
            break;
        }
    }

    ran_to_end = true;
    return false;
}

bool Token_Cache::is_covered(uint64_t position) const {
    uint64_t cpp = 0;
    if (check_points.len() > 0) {
        cpp = check_points.last().position;
    }
    return position < cpp + 1024;
}

struct Job_Syntax_Highlight_Buffer_Data {
    cz::Arc_Weak<Buffer_Handle> handle;
};

static void job_syntax_highlight_buffer_kill(void* _data) {
    Job_Syntax_Highlight_Buffer_Data* data = (Job_Syntax_Highlight_Buffer_Data*)_data;
    data->handle.drop();
    cz::heap_allocator().dealloc(data);
}

static bool job_syntax_highlight_buffer_tick(Asynchronous_Job_Handler*, void* _data) {
    ZoneScoped;

    Job_Syntax_Highlight_Buffer_Data* data = (Job_Syntax_Highlight_Buffer_Data*)_data;

    cz::Arc<Buffer_Handle> handle;
    if (!data->handle.upgrade(&handle)) {
        job_syntax_highlight_buffer_kill(_data);
        return true;
    }
    CZ_DEFER(handle.drop());

    Token_Cache clone = {};
    CZ_DEFER(clone.drop());

    const Buffer* buffer = handle->try_lock_reading();
    if (!buffer) {
        return false;
    }
    CZ_DEFER(handle->unlock());

    bool stop = false;

    {
        ZoneScopedN("job_syntax_highlight_buffer_tick run syntax highlighting");

        if (buffer->token_cache.is_covered(buffer->contents.len)) {
            return true;
        }

        clone = buffer->token_cache.clone();
        clone.update(buffer);

        uint64_t state;
        Contents_Iterator iterator;
        if (buffer->token_cache.check_points.len() > 0) {
            state = buffer->token_cache.check_points.last().state;
            iterator =
                buffer->contents.iterator_at(buffer->token_cache.check_points.last().position);
        } else {
            state = 0;
            iterator = buffer->contents.start();
        }

        for (size_t i = 0; i < 100; ++i) {
            if (!clone.next_check_point(buffer, &iterator, &state)) {
                stop = true;
                break;
            }
        }
    }

    Buffer* buffer_mut = handle->increase_reading_to_writing();

    {
        ZoneScopedN("job_syntax_highlight_buffer_tick record results");

        // Someone else pre-empted us and added a bunch of check points.
        if (clone.check_points.len() < buffer_mut->token_cache.check_points.len()) {
            return false;
        }

        // Update again since we could've been pre-empted before we relocked.
        clone.update(buffer_mut);

        std::swap(buffer_mut->token_cache, clone);
    }

    return stop;
}

Asynchronous_Job job_syntax_highlight_buffer(cz::Arc_Weak<Buffer_Handle> handle) {
    Job_Syntax_Highlight_Buffer_Data* data =
        cz::heap_allocator().alloc<Job_Syntax_Highlight_Buffer_Data>();
    CZ_ASSERT(data);
    data->handle = handle;

    Asynchronous_Job job;
    job.tick = job_syntax_highlight_buffer_tick;
    job.kill = job_syntax_highlight_buffer_kill;
    job.data = data;
    return job;
}

}
