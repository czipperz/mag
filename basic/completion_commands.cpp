#include "completion_commands.hpp"

#include <cz/char_type.hpp>
#include "command_macros.hpp"
#include "commands.hpp"
#include "match.hpp"
#include "movement.hpp"

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

/// Look in the bucket for an identifier that starts with `[start, middle)`,
/// is longer than `middle - start`, and isn't exactly `[start, end)`.
static bool look_in(cz::Slice<char> bucket,
                    Contents_Iterator start,
                    Contents_Iterator middle,
                    uint64_t end,
                    Contents_Iterator* out,
                    bool forward) {
    ZoneScoped;
    const cz::Str str = {bucket.elems, bucket.len};
    const char first = start.get();
    Contents_Iterator test_start = *out;
    if (!forward)
        test_start.advance(str.len);
    size_t index = forward ? 0 : str.len;
    bool first_iteration = true;
    while (1) {
        // Find the start of the test identifier.
        const char* fst;
        if (forward)
            fst = str.slice_start(index).find(first);
        else
            fst = str.slice_end(index).rfind(first);
        if (!fst)
            break;

        size_t old_index = index;
        index = fst - str.buffer;
        if (forward) {
            ++index;
            if (first_iteration) {
                first_iteration = false;
            } else {
                test_start.advance();
            }
        }

        if (forward)
            test_start.advance(fst - (str.buffer + old_index));
        else
            test_start.retreat((str.buffer + old_index) - fst);

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
        if (test_start.position + (middle.position - start.position) >= test_start.contents->len)
            continue;
        Contents_Iterator test_middle = test_start;
        test_middle.advance(middle.position - start.position);
        CZ_DEBUG_ASSERT(test_middle.position < test_middle.contents->len);
        char after = test_middle.get();
        if (!cz::is_alnum(after) && after != '_')
            continue;

        // Check prefix matches.
        if (!matches(start, middle.position, test_start))
            continue;

        // Don't complete exact matches.
        if (matches(middle, end, test_middle)) {
            Contents_Iterator test_end = test_middle;
            test_end.advance(end - middle.position);
            if (test_end.at_eob())
                continue;
            char after_end = test_end.get();
            if (!cz::is_alnum(after_end) && after_end != '_')
                continue;
        }

        *out = test_start;
        return true;
    }
    return false;
}

static bool choose_closer(uint64_t start,
                          uint64_t middle,
                          bool match_backward,
                          bool match_forward,
                          Contents_Iterator backward,
                          Contents_Iterator forward,
                          Contents_Iterator* out) {
    if (match_backward && match_forward) {
        // Choose nearest.
        if (start - (backward.position + middle - start) <= forward.position - middle) {
            *out = backward;
            return true;
        } else {
            *out = forward;
            return true;
        }
    } else if (match_backward) {
        *out = backward;
        return true;
    } else {
        *out = forward;
        return true;
    }
}

bool find_nearest_matching_identifier(Contents_Iterator it,
                                      Contents_Iterator middle,
                                      uint64_t end,
                                      size_t max_buckets,
                                      Contents_Iterator* out) {
    Contents_Iterator backward, forward;
    backward = it;
    backward.retreat(backward.index);
    forward = backward;

    // Search in the shared bucket.
    if (it.bucket < it.contents->buckets.len) {
        cz::Slice<char> bucket = it.contents->buckets[it.bucket];

        // Search backward.
        bool match_backward = look_in({bucket.elems, it.index}, it, middle, end, &backward, false);

        // Search forward.
        Contents_Iterator temp = forward;
        temp.advance(it.index + 1);
        cz::Slice<char> after = {bucket.elems + it.index + 1, bucket.len - it.index - 1};
        bool match_forward = look_in(after, it, middle, end, &temp, true);

        if (match_backward || match_forward)
            return choose_closer(it.position, middle.position, match_backward, match_forward,
                                 backward, temp, out);
    }

    for (size_t i = 0; i < max_buckets; ++i) {
        // Search backward.
        bool match_backward = false;
        if (backward.bucket >= 1) {
            cz::Slice<char> bucket = it.contents->buckets[backward.bucket - 1];
            backward.retreat(bucket.len);
            match_backward = look_in(bucket, it, middle, end, &backward, false);
        }

        // Search forward.
        bool match_forward = false;
        if (forward.bucket + 1 < it.contents->buckets.len) {
            forward.advance(it.contents->buckets[forward.bucket].len);
            cz::Slice<char> bucket = it.contents->buckets[forward.bucket];
            match_forward = look_in(bucket, it, middle, end, &forward, true);
        }

        if (match_backward || match_forward)
            return choose_closer(it.position, middle.position, match_backward, match_forward,
                                 backward, forward, out);
    }

    return false;
}

bool find_nearest_matching_identifier_before(Contents_Iterator it,
                                             Contents_Iterator middle,
                                             uint64_t end,
                                             size_t max_buckets,
                                             Contents_Iterator* out) {
    Contents_Iterator backward, forward;
    backward = it;
    backward.retreat(backward.index);
    forward = backward;

    // Search in the shared bucket.
    if (it.bucket < it.contents->buckets.len) {
        cz::Slice<char> bucket = it.contents->buckets[it.bucket];
        bool match_backward = look_in({bucket.elems, it.index}, it, middle, end, &backward, false);
        if (match_backward) {
            *out = backward;
            return true;
        }
    }

    for (size_t i = 0; i < max_buckets; ++i) {
        // Search backward.
        if (backward.bucket < 1)
            break;

        cz::Slice<char> bucket = it.contents->buckets[backward.bucket - 1];
        backward.retreat(bucket.len);
        bool match_backward = look_in(bucket, it, middle, end, &backward, false);
        if (match_backward) {
            *out = backward;
            return true;
        }
    }

    return false;
}

static void replace_identifier_with_identifier_start_at(Client* client,
                                                        Buffer* buffer,
                                                        Window_Unified* window,
                                                        Contents_Iterator match_start) {
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Contents_Iterator match_end = match_start;
    forward_through_identifier(&match_end);

    SSOStr result =
        buffer->contents.slice(transaction.value_allocator(), match_start, match_end.position);

    // Apply the completion to all cursors.
    uint64_t offset = 0;
    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();
    for (size_t i = 0; i < cursors.len; ++i) {
        it.advance_to(cursors[i].point);

        // Retreat to start of identifier.
        uint64_t middle = it.position;
        backward_through_identifier(&it);
        uint64_t start = it.position;

        if (middle - it.position <= result.len() &&
            matches(it, middle, result.as_str().slice_end(middle - it.position))) {
            it.advance_to(middle);
        } else {
            Edit remove;
            remove.value = buffer->contents.slice(transaction.value_allocator(), it, middle);
            remove.position = it.position + offset;
            remove.flags = Edit::REMOVE;
            transaction.push(remove);
        }

        Edit insert;
        insert.value = SSOStr::from_constant(result.as_str().slice_start(it.position - start));
        insert.position = it.position + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += insert.value.len() - (middle - it.position);
    }

    transaction.commit(client);
}

void command_complete_at_point_nearest_matching(Editor* editor, Command_Source source) {
    ZoneScoped;
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    Contents_Iterator end = it;
    backward_through_identifier(&it);
    forward_through_identifier(&end);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    Contents_Iterator match_start;
    if (!find_nearest_matching_identifier(it, middle, end.position, buffer->contents.buckets.len,
                                          &match_start)) {
        source.client->show_message("No matches");
        return;
    }

    replace_identifier_with_identifier_start_at(source.client, buffer, window, match_start);
}

void command_complete_at_point_nearest_matching_before(Editor* editor, Command_Source source) {
    ZoneScoped;
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    Contents_Iterator end = it;
    backward_through_identifier(&it);
    forward_through_identifier(&end);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    Contents_Iterator match_start;
    if (!find_nearest_matching_identifier_before(it, middle, end.position,
                                                 buffer->contents.buckets.len, &match_start)) {
        source.client->show_message("No matches");
        return;
    }

    replace_identifier_with_identifier_start_at(source.client, buffer, window, match_start);
}

}
}
