#include "token_cache.hpp"

#include <stdlib.h>
#include <chrono>
#include <cz/bit_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/util.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/buffer_handle.hpp"
#include "core/command_macros.hpp"
#include "core/contents.hpp"
#include "core/job.hpp"
#include "core/token.hpp"
#include "core/tracy_format.hpp"

namespace mag {

void Token_Cache::drop() {
    check_points.drop(cz::heap_allocator());
}

void Token_Cache::reset() {
    change_index = 0;
    check_points.len = 0;
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
    size_t end = check_points.len;
    size_t result_index = check_points.len;
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

    if (result_index < check_points.len) {
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
                if (!(edit.flags & Edit::INSERT_MASK) &&
                    edit.position + edit.value.len() >= position) {
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
                if ((edit.flags & Edit::INSERT_MASK) &&
                    edit.position + edit.value.len() >= position) {
                    return true;
                }
                position_before_edit(edit, &position);
            }
        }
    }
    return false;
}

static bool any_changes_inbetween(cz::Slice<const Change> changes, uint64_t start, uint64_t end) {
    for (size_t c = 0; c < changes.len; ++c) {
        if (changes[c].is_redo) {
            for (size_t e = 0; e < changes[c].commit.edits.len; ++e) {
                auto& edit = changes[c].commit.edits[e];
                if (cz::max(edit.position, start) <=
                    cz::min(
                        edit.position + (!(edit.flags & Edit::INSERT_MASK) ? edit.value.len() : 0),
                        end)) {
                    return true;
                }
                position_after_edit(edit, &start);
                position_after_edit(edit, &end);
            }
        } else {
            for (size_t e = changes[c].commit.edits.len; e-- > 0;) {
                auto& edit = changes[c].commit.edits[e];
                if (cz::max(edit.position, start) <=
                    cz::min(
                        edit.position + ((edit.flags & Edit::INSERT_MASK) ? edit.value.len() : 0),
                        end)) {
                    return true;
                }
                position_before_edit(edit, &start);
                position_before_edit(edit, &end);
            }
        }
    }
    return false;
}

bool Token_Cache::update(const Buffer* buffer) {
    ZoneScoped;

    cz::Slice<const Change> changes = buffer->changes;
    cz::Slice<const Change> pending_changes = {changes.elems + change_index,
                                               changes.len - change_index};

    // Most of the time there are no changes so do nothing.
    if (pending_changes.len == 0) {
        return true;
    }

    cz::Sized_Bit_Array changed_check_points;
    changed_check_points.init(cz::heap_allocator(), check_points.len);
    CZ_DEFER(changed_check_points.drop(cz::heap_allocator()));

    // Detect check points that changed
    for (size_t i = 1; i < check_points.len; ++i) {
        if (any_changes_inbetween(pending_changes, check_points[i - 1].position,
                                  check_points[i].position)) {
            changed_check_points.set(i);
        }

        uint64_t pos = check_points[i].position;
        position_after_changes(pending_changes, &pos);

        if (!any_changes_after(pending_changes, check_points[i].position)) {
            uint64_t offset = pos - check_points[i].position;
            for (size_t j = i; j < check_points.len; ++j) {
                check_points[j].position += offset;
            }
            break;
        }

        check_points[i].position = pos;
    }
    change_index = changes.len;

    Contents_Iterator iterator = buffer->contents.start();
    // Fix check points that were changed
    for (size_t i = 1; i < check_points.len; ++i) {
        if (!changed_check_points.get(i))
            continue;

        iterator.advance_to(check_points[i - 1].position);
        uint64_t end_position = check_points[i].position;
        Token token;
        uint64_t state = check_points[i - 1].state;
        size_t start = i;
        // Efficiently loop without recalculating the iterator so long as
        // the edit is screwing up future check points.
        while (i < check_points.len) {
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
                check_points.len = i;
                ran_to_end = false;
                return false;
            }

            bool has_token = false;
            while (iterator.position < end_position) {
#ifndef NDEBUG
                token = INVALID_TOKEN;
#endif

                has_token = buffer->mode.next_token(&iterator, &token, &state);
                if (!has_token) {
                    break;
                }

#ifndef NDEBUG
                token.check_valid(buffer->contents.len);
#endif
            }
            if (!has_token) {
                check_points.len = i;
                return true;
            }

            if (token.end != check_points[i].position || state != check_points[i].state) {
                check_points[i].position = token.end;
                check_points[i].state = state;
                ++i;
                if (i == check_points.len) {
                    return true;
                }
                end_position = check_points[i].position;
            } else {
                break;
            }
        }
    }

    return true;
}

void Token_Cache::generate_check_points_until(const Buffer* buffer, uint64_t position) {
    ZoneScoped;

    uint64_t state;
    Contents_Iterator iterator;
    if (check_points.len > 0) {
        state = check_points.last().state;
        iterator = buffer->contents.iterator_at(check_points.last().position);
    } else {
        state = 0;
        iterator = buffer->contents.start();

        // Put an empty check point at the start.
        check_points.reserve(cz::heap_allocator(), 1);
        check_points.push({});
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

#ifndef NDEBUG
        token = INVALID_TOKEN;
#endif

        if (!buffer->mode.next_token(iterator, &token, state)) {
            break;
        }

#ifndef NDEBUG
        token.check_valid(buffer->contents.len);
#endif
    }

    ran_to_end = true;
    return false;
}

bool Token_Cache::is_covered(uint64_t position) const {
    // If the tokenizer crashes halfway through then we
    // consider points after the crash to be covered.
    if (ran_to_end) {
        return true;
    }

    uint64_t cpp = 0;
    if (check_points.len > 0) {
        cpp = check_points.last().position;
    }
    return position < cpp + 1024;
}

struct Job_Syntax_Highlight_Buffer_Data {
    cz::Arc_Weak<Buffer_Handle> handle;
    Token_Cache token_cache;
};

static void job_syntax_highlight_buffer_kill(void* _data) {
    Job_Syntax_Highlight_Buffer_Data* data = (Job_Syntax_Highlight_Buffer_Data*)_data;
    data->handle.drop();
    data->token_cache.drop();
    cz::heap_allocator().dealloc(data);
}

static Job_Tick_Result job_syntax_highlight_buffer_tick(Asynchronous_Job_Handler*, void* _data) {
    ZoneScoped;

    Job_Syntax_Highlight_Buffer_Data* data = (Job_Syntax_Highlight_Buffer_Data*)_data;

    cz::Arc<Buffer_Handle> handle;
    if (!data->handle.upgrade(&handle)) {
        job_syntax_highlight_buffer_kill(_data);
        return Job_Tick_Result::FINISHED;
    }
    CZ_DEFER(handle.drop());

    const Buffer* buffer = handle->try_lock_reading();
    if (!buffer) {
        return Job_Tick_Result::STALLED;
    }
    CZ_DEFER(handle->unlock());

    bool stop = false;

    {
        ZoneScopedN("job_syntax_highlight_buffer_tick run syntax highlighting");

        if (buffer->token_cache.is_covered(buffer->contents.len)) {
            job_syntax_highlight_buffer_kill(_data);
            return Job_Tick_Result::FINISHED;
        }

        if (data->token_cache.check_points.len != buffer->token_cache.check_points.len ||
            data->token_cache.change_index != buffer->token_cache.change_index) {
            data->token_cache.change_index = buffer->token_cache.change_index;
            data->token_cache.check_points.len = 0;
            data->token_cache.check_points.reserve(cz::heap_allocator(),
                                                   buffer->token_cache.check_points.len);
            data->token_cache.check_points.append(buffer->token_cache.check_points);
            data->token_cache.ran_to_end = buffer->token_cache.ran_to_end;
        }

        data->token_cache.update(buffer);

        uint64_t state;
        Contents_Iterator iterator;
        if (buffer->token_cache.check_points.len > 0) {
            state = buffer->token_cache.check_points.last().state;
            iterator =
                buffer->contents.iterator_at(buffer->token_cache.check_points.last().position);
        } else {
            state = 0;
            iterator = buffer->contents.start();
        }

        auto time_start = std::chrono::steady_clock::now();
        while (1) {
            if (!data->token_cache.next_check_point(buffer, &iterator, &state)) {
                stop = true;
                break;
            }

            if (std::chrono::steady_clock::now() - time_start > std::chrono::milliseconds(2)) {
                break;
            }
        }
    }

    Buffer* buffer_mut = handle->increase_reading_to_writing();

    {
        ZoneScopedN("job_syntax_highlight_buffer_tick record results");

        // Someone else pre-empted us and added a bunch of check points.
        if (data->token_cache.check_points.len < buffer_mut->token_cache.check_points.len) {
            return Job_Tick_Result::MADE_PROGRESS;
        }

        // Update again since we could've been pre-empted before we relocked.
        if (!data->token_cache.update(buffer_mut)) {
            return Job_Tick_Result::MADE_PROGRESS;
        }

        cz::swap(buffer_mut->token_cache, data->token_cache);
    }

    if (stop) {
        job_syntax_highlight_buffer_kill(_data);
    }
    return stop ? Job_Tick_Result::FINISHED : Job_Tick_Result::MADE_PROGRESS;
}

Asynchronous_Job job_syntax_highlight_buffer(cz::Arc_Weak<Buffer_Handle> handle) {
    ZoneScoped;
    Job_Syntax_Highlight_Buffer_Data* data =
        cz::heap_allocator().alloc<Job_Syntax_Highlight_Buffer_Data>();
    CZ_ASSERT(data);
    *data = {};
    data->handle = handle;

    Asynchronous_Job job;
    job.tick = job_syntax_highlight_buffer_tick;
    job.kill = job_syntax_highlight_buffer_kill;
    job.data = data;
    return job;
}

}
