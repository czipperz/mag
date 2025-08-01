#include "completion_commands.hpp"

#include <cz/char_type.hpp>
#include <cz/dedup.hpp>
#include <cz/sort.hpp>
#include "commands.hpp"
#include "core/command_macros.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"

namespace mag {
namespace basic {

REGISTER_COMMAND(command_insert_completion);
void command_insert_completion(Editor* editor, Command_Source source) {
    const Completion_Cache* completion_cache = &source.client->mini_buffer_completion_cache;
    if (completion_cache->filter_context.selected >= completion_cache->filter_context.results.len) {
        return;
    }

    cz::String result = {};
    CZ_DEFER(result.drop(cz::heap_allocator()));
    result.reserve_exact(
        cz::heap_allocator(),
        completion_cache->filter_context.results[completion_cache->filter_context.selected].len +
            completion_cache->engine_context.result_prefix.len +
            completion_cache->engine_context.result_suffix.len);
    result.append(completion_cache->engine_context.result_prefix);
    result.append(
        completion_cache->filter_context.results[completion_cache->filter_context.selected]);
    result.append(completion_cache->engine_context.result_suffix);

    Window_Unified* window = source.client->mini_buffer_window();
    WITH_WINDOW_BUFFER(window, source.client);

    cz::String query = buffer->contents.stringify(cz::heap_allocator());
    CZ_DEFER(query.drop(cz::heap_allocator()));

    size_t common_base;
    for (common_base = 0; common_base < query.len && common_base < result.len; ++common_base) {
        if (query[common_base] != result[common_base]) {
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

    bool inserting = common_base < result.len;
    if (inserting) {
        Edit insert;
        insert.value =
            SSOStr::as_duplicate(transaction.value_allocator(), result.slice_start(common_base));
        insert.position = common_base;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(source.client);
}

REGISTER_COMMAND(command_insert_completion_and_submit_mini_buffer);
void command_insert_completion_and_submit_mini_buffer(Editor* editor, Command_Source source) {
    command_insert_completion(editor, source);
    command_submit_mini_buffer(editor, source);
}

REGISTER_COMMAND(command_next_completion);
void command_next_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected + 1 >= context->results.len) {
        return;
    }
    ++context->selected;
}

REGISTER_COMMAND(command_previous_completion);
void command_previous_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected == 0) {
        return;
    }
    --context->selected;
}

REGISTER_COMMAND(command_completion_down_page);
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

REGISTER_COMMAND(command_completion_up_page);
void command_completion_up_page(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    if (context->selected < editor->theme.max_completion_results) {
        context->selected = 0;
        return;
    }
    context->selected -= editor->theme.max_completion_results;
}

REGISTER_COMMAND(command_first_completion);
void command_first_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    context->selected = 0;
}

REGISTER_COMMAND(command_last_completion);
void command_last_completion(Editor* editor, Command_Source source) {
    Completion_Filter_Context* context =
        &source.client->mini_buffer_completion_cache.filter_context;
    context->selected = context->results.len;
    if (context->selected > 0) {
        --context->selected;
    }
}

/// Look in the bucket for an identifier that starts with
/// `[start, middle)`, is longer than `middle - start`.
static bool look_in(cz::Slice<char> bucket,
                    Contents_Iterator start,
                    Contents_Iterator middle,
                    cz::Slice<uint64_t> ignored_positions,
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

        Contents_Iterator test_end = test_middle;
        forward_through_identifier(&test_end);

        // Check it is not in the ignored list.
        bool in_ignore_list = false;
        for (size_t i = 0; i < ignored_positions.len; ++i) {
            if (test_start.position < ignored_positions[i] &&
                test_end.position > ignored_positions[i]) {
                in_ignore_list = true;
                break;
            }
        }
        if (in_ignore_list)
            continue;

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
                                      size_t max_buckets,
                                      cz::Slice<uint64_t> ignored_positions,
                                      Contents_Iterator* out) {
    Contents_Iterator backward, forward;
    backward = it;
    backward.retreat(backward.index);
    forward = backward;

    // Search in the shared bucket.
    if (it.bucket < it.contents->buckets.len) {
        cz::Slice<char> bucket = it.contents->buckets[it.bucket];

        // Search backward.
        bool match_backward =
            look_in({bucket.elems, it.index}, it, middle, ignored_positions, &backward, false);

        // Search forward.
        Contents_Iterator temp = forward;
        temp.advance(it.index + 1);
        cz::Slice<char> after = {bucket.elems + it.index + 1, bucket.len - it.index - 1};
        bool match_forward = look_in(after, it, middle, ignored_positions, &temp, true);

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
            match_backward = look_in(bucket, it, middle, ignored_positions, &backward, false);
        }

        // Search forward.
        bool match_forward = false;
        if (forward.bucket + 1 < it.contents->buckets.len) {
            forward.advance(it.contents->buckets[forward.bucket].len);
            cz::Slice<char> bucket = it.contents->buckets[forward.bucket];
            match_forward = look_in(bucket, it, middle, ignored_positions, &forward, true);
        }

        if (match_backward || match_forward)
            return choose_closer(it.position, middle.position, match_backward, match_forward,
                                 backward, forward, out);
    }

    return false;
}

bool find_nearest_matching_identifier_before(Contents_Iterator it,
                                             Contents_Iterator middle,
                                             size_t max_buckets,
                                             cz::Slice<uint64_t> ignored_positions,
                                             Contents_Iterator* out) {
    Contents_Iterator backward;
    backward = it;
    backward.retreat(backward.index);

    // Search in the shared bucket.
    if (it.bucket < it.contents->buckets.len) {
        cz::Slice<char> bucket = it.contents->buckets[it.bucket];
        bool match_backward =
            look_in({bucket.elems, it.index}, it, middle, ignored_positions, &backward, false);
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
        bool match_backward = look_in(bucket, it, middle, ignored_positions, &backward, false);
        if (match_backward) {
            *out = backward;
            return true;
        }
    }

    return false;
}

bool find_nearest_matching_identifier_after(Contents_Iterator it,
                                            Contents_Iterator middle,
                                            size_t max_buckets,
                                            cz::Slice<uint64_t> ignored_positions,
                                            Contents_Iterator* out) {
    Contents_Iterator forward;
    forward = it;
    forward.retreat(forward.index);

    // Search in the shared bucket.
    if (it.bucket < it.contents->buckets.len) {
        cz::Slice<char> bucket = it.contents->buckets[it.bucket];
        Contents_Iterator temp = forward;
        temp.advance(it.index + 1);
        cz::Slice<char> after = {bucket.elems + it.index + 1, bucket.len - it.index - 1};
        bool match_forward = look_in(after, it, middle, ignored_positions, &temp, true);
        if (match_forward) {
            *out = temp;
            return true;
        }
    }

    for (size_t i = 0; i < max_buckets; ++i) {
        if (forward.bucket + 1 >= it.contents->buckets.len)
            break;

        forward.advance(it.contents->buckets[forward.bucket].len);
        cz::Slice<char> bucket = it.contents->buckets[forward.bucket];
        bool match_forward = look_in(bucket, it, middle, ignored_positions, &forward, true);
        if (match_forward) {
            *out = forward;
            return true;
        }
    }

    return false;
}

bool find_nearest_matching_identifier_before_after(Contents_Iterator it,
                                                   Contents_Iterator middle,
                                                   size_t max_buckets,
                                                   cz::Slice<uint64_t> ignored_positions,
                                                   Contents_Iterator* out) {
    if (find_nearest_matching_identifier_before(it, middle, max_buckets, ignored_positions, out))
        return true;
    if (find_nearest_matching_identifier_after(it, middle, max_buckets, ignored_positions, out))
        return true;
    return false;
}

static void append_identifier_suffix(Client* client,
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

    Contents_Iterator it = buffer->contents.start();
    cz::Slice<Cursor> cursors = window->cursors;
    it.advance_to(cursors[window->selected_cursor].point);
    uint64_t middle = it.position;
    backward_through_identifier(&it);
    uint64_t start = it.position;
    cz::Str result_str =
        result.as_str().slice_start(cz::min((uint64_t)result.len(), middle - start));

    // Apply the completion to all cursors.
    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Edit insert;
        insert.value = SSOStr::from_constant(result_str);
        insert.position = cursors[i].point + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);

        offset += result_str.len;
    }

    transaction.commit(client);
}

REGISTER_COMMAND(command_complete_at_point_nearest_matching);
void command_complete_at_point_nearest_matching(Editor* editor, Command_Source source) {
    ZoneScoped;
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    backward_through_identifier(&it);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    cz::Vector<uint64_t> cursor_positions = {};
    CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
    cursor_positions.reserve_exact(cz::heap_allocator(), window->cursors.len);
    for (size_t i = 0; i < window->cursors.len; ++i) {
        cursor_positions.push(window->cursors[i].point);
    }

    Contents_Iterator match_start;
    if (!find_nearest_matching_identifier(it, middle, buffer->contents.buckets.len,
                                          /*ignored_positions=*/cursor_positions, &match_start)) {
        source.client->show_message("No matches");
        return;
    }

    append_identifier_suffix(source.client, buffer, window, match_start);
}

REGISTER_COMMAND(command_complete_at_point_nearest_matching_before);
void command_complete_at_point_nearest_matching_before(Editor* editor, Command_Source source) {
    ZoneScoped;
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    backward_through_identifier(&it);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    cz::Vector<uint64_t> cursor_positions = {};
    CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
    cursor_positions.reserve_exact(cz::heap_allocator(), window->cursors.len);
    for (size_t i = 0; i < window->cursors.len; ++i) {
        cursor_positions.push(window->cursors[i].point);
    }

    Contents_Iterator match_start;
    if (!find_nearest_matching_identifier_before(it, middle, buffer->contents.buckets.len,
                                                 /*ignored_positions=*/cursor_positions,
                                                 &match_start)) {
        source.client->show_message("No matches");
        return;
    }

    append_identifier_suffix(source.client, buffer, window, match_start);
}

REGISTER_COMMAND(command_complete_at_point_nearest_matching_after);
void command_complete_at_point_nearest_matching_after(Editor* editor, Command_Source source) {
    ZoneScoped;
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    backward_through_identifier(&it);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    cz::Vector<uint64_t> cursor_positions = {};
    CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
    cursor_positions.reserve_exact(cz::heap_allocator(), window->cursors.len);
    for (size_t i = 0; i < window->cursors.len; ++i) {
        cursor_positions.push(window->cursors[i].point);
    }

    Contents_Iterator match_start;
    if (!find_nearest_matching_identifier_after(it, middle, buffer->contents.buckets.len,
                                                /*ignored_positions=*/cursor_positions,
                                                &match_start)) {
        source.client->show_message("No matches");
        return;
    }

    append_identifier_suffix(source.client, buffer, window, match_start);
}

REGISTER_COMMAND(command_complete_at_point_nearest_matching_before_after);
void command_complete_at_point_nearest_matching_before_after(Editor* editor,
                                                             Command_Source source) {
    ZoneScoped;
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it =
        buffer->contents.iterator_at(window->cursors[window->selected_cursor].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    backward_through_identifier(&it);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    cz::Vector<uint64_t> cursor_positions = {};
    CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
    cursor_positions.reserve_exact(cz::heap_allocator(), window->cursors.len);
    for (size_t i = 0; i < window->cursors.len; ++i) {
        cursor_positions.push(window->cursors[i].point);
    }

    Contents_Iterator match_start;
    if (!find_nearest_matching_identifier_before_after(it, middle, buffer->contents.buckets.len,
                                                       /*ignored_positions=*/cursor_positions,
                                                       &match_start)) {
        source.client->show_message("No matches");
        return;
    }

    append_identifier_suffix(source.client, buffer, window, match_start);
}

///////////////////////////////////////////////////////////////////////////////
// command_complete_at_point_prompt_identifiers
///////////////////////////////////////////////////////////////////////////////

/// Look in the bucket for an identifier that starts with `query` and is longer than `query`.
static bool list_all_in(cz::Slice<char> bucket,
                        Contents_Iterator out,
                        cz::Str query,
                        cz::Allocator result_allocator,
                        cz::Heap_Vector<cz::Str>* results) {
    ZoneScoped;
    const cz::Str str = {bucket.elems, bucket.len};
    const char first = query[0];
    Contents_Iterator test_start = out;
    size_t index = 0;
    bool first_iteration = true;
    while (1) {
        // Find the start of the test identifier.
        const char* fst = str.slice_start(index).find(first);
        if (!fst)
            break;

        size_t old_index = index;
        index = fst - str.buffer;

        ++index;
        if (first_iteration) {
            first_iteration = false;
        } else {
            test_start.advance();
        }

        test_start.advance(fst - (str.buffer + old_index));

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
        if (test_start.position + query.len >= test_start.contents->len)
            continue;
        Contents_Iterator test_middle = test_start;
        test_middle.advance(query.len);
        CZ_DEBUG_ASSERT(test_middle.position < test_middle.contents->len);
        char after = test_middle.get();
        if (!cz::is_alnum(after) && after != '_')
            continue;

        // Check prefix matches.
        if (!looking_at(test_start, query))
            continue;

        Contents_Iterator test_end = test_start;
        forward_through_identifier(&test_end);
        cz::String result = {};
        test_start.contents->slice_into(result_allocator, test_start, test_end.position, &result);

        results->reserve(1);
        results->push(result);
    }
    return false;
}

void all_identifiers_starting_with(const Contents& contents,
                                   cz::Str query,
                                   cz::Allocator allocator,
                                   cz::Heap_Vector<cz::Str>* results) {
    Contents_Iterator iterator = contents.start();
    while (!iterator.at_eob()) {
        cz::Slice<char> bucket = contents.buckets[iterator.bucket];
        list_all_in(bucket, iterator, query, allocator, results);
        iterator.advance(bucket.len);
    }
}

bool Identifier_Completion_Engine_Data::load(cz::Allocator allocator,
                                             cz::Heap_Vector<cz::Str>* results) {
    Identifier_Completion_Engine_Data* data = this;
    cz::Arc<Buffer_Handle> handle;
    if (!data->handle.upgrade(&handle)) {
        return false;
    }
    CZ_DEFER(handle.drop());

    WITH_CONST_BUFFER_HANDLE(handle);
    all_identifiers_starting_with(buffer->contents, data->query, allocator, results);
    return true;
}

static bool identifier_completion_engine(Editor* editor,
                                         Completion_Engine_Context* context,
                                         bool is_initial_frame) {
    ZoneScoped;
    if (context->results.len > 0) {
        return false;
    }

    context->results_buffer_array.clear();
    context->results.len = 0;

    auto data = (Identifier_Completion_Engine_Data*)context->data;
    if (!data->load(context->results_buffer_array.allocator(), &context->results)) {
        return false;
    }

    cz::sort(context->results);
    cz::dedup(&context->results);

    return true;
}

REGISTER_COMMAND(command_complete_at_point_prompt_identifiers);
void command_complete_at_point_prompt_identifiers(Editor* editor, Command_Source source) {
    ZoneScoped;

    WITH_CONST_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    backward_through_identifier(&it);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    Identifier_Completion_Engine_Data* data =
        cz::heap_allocator().alloc<Identifier_Completion_Engine_Data>();
    data->query = {};
    buffer->contents.slice_into(cz::heap_allocator(), it, middle.position, &data->query);
    data->handle = handle.clone_downgrade();

    window->start_completion(identifier_completion_engine);
    window->completion_cache.engine_context.reset();

    window->completion_cache.engine_context.data = data;
    window->completion_cache.engine_context.cleanup = [](void* _data) {
        auto data = (Identifier_Completion_Engine_Data*)_data;
        data->query.drop(cz::heap_allocator());
        data->handle.drop();
        cz::heap_allocator().dealloc(data);
    };
}

///////////////////////////////////////////////////////////////////////////////
// command_copy_rest_of_line_from_nearest_matching_identifier
///////////////////////////////////////////////////////////////////////////////

REGISTER_COMMAND(command_copy_rest_of_line_from_nearest_matching_identifier);
void command_copy_rest_of_line_from_nearest_matching_identifier(Editor* editor,
                                                                Command_Source source) {
    ZoneScoped;
    WITH_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    backward_through_identifier(&it);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    cz::Vector<uint64_t> cursor_positions = {};
    CZ_DEFER(cursor_positions.drop(cz::heap_allocator()));
    cursor_positions.reserve_exact(cz::heap_allocator(), window->cursors.len);
    for (size_t i = 0; i < window->cursors.len; ++i) {
        cursor_positions.push(window->cursors[i].point);
    }

    Contents_Iterator match_start;
    if (!find_nearest_matching_identifier_before_after(it, middle, buffer->contents.buckets.len,
                                                       /*ignored_positions=*/cursor_positions,
                                                       &match_start)) {
        source.client->show_message("No matches");
        return;
    }

    match_start.advance(middle.position - it.position);

    Contents_Iterator match_end = match_start;
    end_of_line(&match_end);

    // Insert from the identifier to the end of the line.
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    SSOStr value =
        buffer->contents.slice(transaction.value_allocator(), match_start, match_end.position);

    cz::Slice<Cursor> cursors = window->cursors;
    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        Edit edit;
        edit.value = value;
        edit.position = cursors[i].point + offset;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
        offset += value.len();
    }

    transaction.commit(source.client);
}

}
}
