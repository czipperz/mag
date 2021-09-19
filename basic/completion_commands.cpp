#include "completion_commands.hpp"

#include <cz/char_type.hpp>
#include "command_macros.hpp"
#include "commands.hpp"
#include "match.hpp"

namespace mag {
namespace basic {

void command_insert_completion(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->mini_buffer_window();
    WITH_WINDOW_BUFFER(window);
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    cz::Str query = source.client->mini_buffer_completion_cache.engine_context.query;
    if (context->selected >= context->results.len) {
        return;
    }

    cz::Str value = context->results[context->selected];

    size_t common_base;
    for (common_base = 0; common_base < query.len && common_base < value.len; ++common_base) {
        if (query[common_base] != value[common_base]) {
            break;
        }
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    bool removing = common_base < query.len;
    if (removing) {
        Edit remove;
        remove.value =
            SSOStr::as_duplicate(transaction.value_allocator(), query.slice_start(common_base));
        remove.position = common_base;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);
    }

    bool inserting = common_base < value.len;
    if (inserting) {
        Edit insert;
        insert.value =
            SSOStr::as_duplicate(transaction.value_allocator(), value.slice_start(common_base));
        insert.position = common_base;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(source.client);
}

void command_insert_completion_and_submit_mini_buffer(Editor* editor, Command_Source source) {
    command_insert_completion(editor, source);
    command_submit_mini_buffer(editor, source);
}

void command_next_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected + 1 >= context->results.len) {
        return;
    }
    ++context->selected;
}

void command_previous_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected == 0) {
        return;
    }
    --context->selected;
}

void command_completion_down_page(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected + editor->theme.max_completion_results >= context->results.len) {
        context->selected = context->results.len;
        if (context->selected > 0) {
            --context->selected;
        }
        return;
    }
    context->selected += editor->theme.max_completion_results;
}

void command_completion_up_page(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected < editor->theme.max_completion_results) {
        context->selected = 0;
        return;
    }
    context->selected -= editor->theme.max_completion_results;
}

void command_first_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    context->selected = 0;
}

void command_last_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    context->selected = context->results.len;
    if (context->selected > 0) {
        --context->selected;
    }
}

/// Look in the bucket for an identifier that starts with
/// `[start, end)` and is longer than `end - start`.
static bool look_in(cz::Slice<char> bucket,
                    Contents_Iterator start,
                    uint64_t end,
                    Contents_Iterator* out,
                    bool forward) {
    ZoneScoped;
    const cz::Str str = {bucket.elems, bucket.len};
    const char first = start.get();
    Contents_Iterator test_start = *out;
    CZ_DEBUG_ASSERT(test_start.index == 0);
    size_t index = forward ? 0 : str.len;
    while (1) {
        // Find the start of the test identifier.
        const char* fst;
        if (forward)
            fst = str.slice_start(index).find(first);
        else
            fst = str.slice_end(index).rfind(first);
        if (!fst)
            break;

        index = fst - str.buffer;
        if (forward)
            ++index;

        test_start.advance(fst - str.buffer - test_start.index);

        // If character before is an identifier character then
        // `test_start` is not at the start of an identifier.
        if (!test_start.at_bob()) {
            Contents_Iterator temp = test_start;
            temp.retreat();
            char before = temp.get();
            if (cz::is_alnum(before) || before == '_')
                continue;
        }

        // Check character after region is an identifier.
        if (test_start.position + (end - start.position) >= test_start.contents->len)
            continue;
        Contents_Iterator test_end = test_start;
        test_end.advance(end - start.position);
        CZ_DEBUG_ASSERT(test_end.position < test_end.contents->len);
        char after = test_end.get();
        if (!cz::is_alnum(after) && after != '_')
            continue;

        // Check prefix matches.
        if (!matches(start, end, test_start))
            continue;

        *out = test_start;
        return true;
    }
    return false;
}

static bool search_forward_and_backward(Contents_Iterator it,
                                        uint64_t end,
                                        Contents_Iterator* out) {
    Contents_Iterator backward, forward;
    backward = it;
    backward.retreat(backward.index);
    forward = backward;

    // Search in the shared bucket.
    if (it.bucket < it.contents->buckets.len) {
        cz::Slice<char> bucket = it.contents->buckets[it.bucket];
        if (look_in({bucket.elems, it.index}, it, end, &backward, false)) {
            *out = backward;
            return true;
        }
        if (look_in({bucket.elems + it.index, bucket.len - it.index}, it, end, &backward, true)) {
            *out = backward;
            return true;
        }
    }

    for (size_t i = 0; i < it.contents->buckets.len; ++i) {
        // Search backward.
        if (backward.bucket >= 1) {
            cz::Slice<char> bucket = it.contents->buckets[backward.bucket - 1];
            backward.retreat(bucket.len);
            if (look_in(bucket, it, end, &backward, false)) {
                *out = backward;
                return true;
            }
        }

        // Search forward.
        if (forward.bucket + 1 < it.contents->buckets.len) {
            forward.advance(it.contents->buckets[forward.bucket].len);
            cz::Slice<char> bucket = it.contents->buckets[forward.bucket];
            if (look_in(bucket, it, end, &forward, true)) {
                *out = forward;
                return true;
            }
        }
    }

    return false;
}

void command_complete_at_point_nearest_matching(Editor* editor, Command_Source source) {
    ZoneScoped;
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.iterator_at(cursors[0].point);

    // Retreat to start of identifier.
    uint64_t end = it.position;
    while (!it.at_bob()) {
        it.retreat();
        char ch = it.get();
        if (!cz::is_alnum(ch) && ch != '_') {
            it.advance();
            break;
        }
    }

    if (it.position >= end) {
        source.client->show_message("Not at an identifier");
        return;
    }

    Contents_Iterator match_start;
    if (!search_forward_and_backward(it, end, &match_start)) {
        source.client->show_message("No matches");
        return;
    }

    Contents_Iterator match_end = match_start;
    for (; !match_end.at_eob(); match_end.advance()) {
        char ch = match_end.get();
        if (!cz::is_alnum(ch) && ch != '_') {
            break;
        }
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    SSOStr result =
        buffer->contents.slice(transaction.value_allocator(), match_start, match_end.position);

    // Apply the completion to all cursors.
    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        it.advance_to(cursors[i].point);

        // Retreat to start of identifier.
        uint64_t end = it.position;
        while (!it.at_bob()) {
            it.retreat();
            char ch = it.get();
            if (!cz::is_alnum(ch) && ch != '_') {
                it.advance();
                break;
            }
        }

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), it, end);
        remove.position = it.position + offset;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = result;
        insert.position = it.position + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += insert.value.len() - remove.value.len();
    }

    transaction.commit(source.client);
}

}
}
