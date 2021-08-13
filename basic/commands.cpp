#include "commands.hpp"

#include <cz/char_type.hpp>
#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/option.hpp>
#include <cz/parse.hpp>
#include <cz/path.hpp>
#include <cz/process.hpp>
#include <cz/sort.hpp>
#include <cz/util.hpp>
#include "command_macros.hpp"
#include "comment.hpp"
#include "file.hpp"
#include "insert.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "program_info.hpp"
#include "region_movement_commands.hpp"
#include "transaction.hpp"
#include "visible_region.hpp"
#include "window.hpp"

namespace mag {
namespace basic {

void command_do_nothing(Editor* editor, Command_Source source) {}

void command_invalid(Editor* editor, Command_Source source) {
    // Print a message that this key press failed.
    cz::String message = {};
    CZ_DEFER(message.drop(cz::heap_allocator()));

    cz::Str prefix;
    if (source.keys.len == 1) {
        prefix = "Unbound key:";
    } else {
        prefix = "Unbound key chain:";
    }
    message.reserve(cz::heap_allocator(),
                    prefix.len + (1 + stringify_key_max_size) * source.keys.len);
    message.append(prefix);

    CZ_DEBUG_ASSERT(source.keys.len > 0);
    for (size_t i = 0; i < source.keys.len; ++i) {
        message.push(' ');
        stringify_key(&message, source.keys[i]);
    }

    source.client->show_message(message);
}

void command_toggle_read_only(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->read_only = !buffer->read_only;
}

void command_toggle_pinned(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    window->pinned = !window->pinned;
}

void command_toggle_draw_line_numbers(Editor* editor, Command_Source source) {
    editor->theme.draw_line_numbers = !editor->theme.draw_line_numbers;
}

void command_toggle_line_feed(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->use_carriage_returns = !buffer->use_carriage_returns;
}

void command_toggle_render_bucket_boundaries(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->mode.render_bucket_boundaries = !buffer->mode.render_bucket_boundaries;
}

void command_toggle_use_tabs(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->mode.use_tabs = !buffer->mode.use_tabs;
}

void command_toggle_animated_scrolling(Editor* editor, Command_Source source) {
    editor->theme.allow_animated_scrolling = !editor->theme.allow_animated_scrolling;
}

void command_toggle_wrap_long_lines(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    buffer->mode.wrap_long_lines = !buffer->mode.wrap_long_lines;
}

static void command_configure_callback(Editor* editor, Client* client, cz::Str query, void* _data) {
    if (query == "animated scrolling") {
        editor->theme.allow_animated_scrolling = !editor->theme.allow_animated_scrolling;
    } else if (query == "buffer indent width") {
        Dialog dialog = {};
        dialog.prompt = "Set indent width to: ";
        dialog.response_callback = [](Editor* editor, Client* client, cz::Str str, void*) {
            WITH_SELECTED_BUFFER(client);
            uint32_t value;
            if (cz::parse(str, &value) <= 0 || value == 0) {
                client->show_message("Invalid indent width");
                return;
            }
            buffer->mode.indent_width = value;
        };

        cz::Heap_String old = {};
        CZ_DEFER(old.drop());
        {
            WITH_CONST_SELECTED_BUFFER(client);
            old = cz::format(buffer->mode.indent_width);
            dialog.mini_buffer_contents = old;
        }

        client->show_dialog(dialog);
    } else if (query == "buffer tab width") {
        Dialog dialog = {};
        dialog.prompt = "Set tab width to: ";
        dialog.response_callback = [](Editor* editor, Client* client, cz::Str str, void*) {
            WITH_SELECTED_BUFFER(client);
            uint32_t value;
            if (cz::parse(str, &value) <= 0 || value == 0) {
                client->show_message("Invalid tab width");
                return;
            }
            buffer->mode.tab_width = value;
        };

        cz::Heap_String old = {};
        CZ_DEFER(old.drop());
        {
            WITH_CONST_SELECTED_BUFFER(client);
            old = cz::format(buffer->mode.tab_width);
            dialog.mini_buffer_contents = old;
        }

        client->show_dialog(dialog);
    } else if (query == "buffer use tabs") {
        bool use_tabs;
        {
            WITH_SELECTED_BUFFER(client);
            use_tabs = buffer->mode.use_tabs = !buffer->mode.use_tabs;
        }
        if (use_tabs) {
            client->show_message("Buffer now uses tabs");
        } else {
            client->show_message("Buffer now does not use tabs");
        }
    } else if (query == "buffer line feed crlf") {
        WITH_SELECTED_BUFFER(client);
        buffer->use_carriage_returns = !buffer->use_carriage_returns;
    } else if (query == "buffer pinned") {
        Window_Unified* window = client->selected_window();
        window->pinned = !window->pinned;
    } else if (query == "buffer preferred column") {
        Dialog dialog = {};
        dialog.prompt = "Set preferred column to: ";
        dialog.response_callback = [](Editor* editor, Client* client, cz::Str str, void*) {
            WITH_SELECTED_BUFFER(client);
            uint32_t value;
            if (cz::parse(str, &value) <= 0 || value == 0) {
                client->show_message("Invalid preferred column");
                return;
            }
            buffer->mode.preferred_column = value;
        };

        cz::Heap_String old = {};
        CZ_DEFER(old.drop());
        {
            WITH_CONST_SELECTED_BUFFER(client);
            old = cz::format(buffer->mode.preferred_column);
            dialog.mini_buffer_contents = old;
        }

        client->show_dialog(dialog);
    } else if (query == "buffer read only") {
        WITH_SELECTED_BUFFER(client);
        buffer->read_only = !buffer->read_only;
    } else if (query == "buffer render bucket boundaries") {
        WITH_SELECTED_BUFFER(client);
        buffer->mode.render_bucket_boundaries = !buffer->mode.render_bucket_boundaries;
    } else if (query == "buffer wrap long lines") {
        WITH_SELECTED_BUFFER(client);
        buffer->mode.wrap_long_lines = !buffer->mode.wrap_long_lines;
    } else if (query == "draw line numbers") {
        editor->theme.draw_line_numbers = !editor->theme.draw_line_numbers;
    } else if (query == "font size") {
        Dialog dialog = {};
        dialog.prompt = "Set font size to: ";
        dialog.response_callback = [](Editor* editor, Client* client, cz::Str str, void*) {
            uint32_t value;
            if (cz::parse(str, &value) <= 0 || value == 0) {
                client->show_message("Invalid font size (only ints for now)");
                return;
            }
            editor->theme.font_size = value;
        };
        cz::Heap_String old = cz::format(editor->theme.font_size);
        CZ_DEFER(old.drop());
        dialog.mini_buffer_contents = old;
        client->show_dialog(dialog);
    } else {
        client->show_message("Invalid configuration variable");
    }
}

static bool configurations_completion_engine(Editor* editor,
                                             Completion_Engine_Context* context,
                                             bool is_initial_frame) {
    if (context->results.len() != 0) {
        return false;
    }

    context->results.reserve(12);
    context->results.push("buffer indent width");
    context->results.push("buffer tab width");
    context->results.push("buffer use tabs");
    context->results.push("buffer line feed crlf");
    context->results.push("buffer pinned");
    context->results.push("buffer preferred column");
    context->results.push("buffer read only");
    context->results.push("buffer render bucket boundaries");
    context->results.push("buffer wrap long lines");
    context->results.push("animated scrolling");
    context->results.push("draw line numbers");
    context->results.push("font size");
    return true;
}

void command_configure(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Configuration to change: ";
    dialog.completion_engine = configurations_completion_engine;
    dialog.response_callback = command_configure_callback;
    source.client->show_dialog(dialog);
}

void command_show_marks(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    window->show_marks = true;
}

void command_set_mark(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        cursors[c].mark = cursors[c].point;
    }
    window->show_marks = true;
}

void command_swap_mark_point(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();
    cz::Slice<Cursor> cursors = window->cursors;
    for (size_t c = 0; c < cursors.len; ++c) {
        cz::swap(cursors[c].point, cursors[c].mark);
    }

    cz::sort(cursors,
             [](const Cursor* left, const Cursor* right) { return left->point < right->point; });
}

void command_forward_char(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(forward_char);
}

void command_backward_char(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(backward_char);
}

void command_forward_word(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(forward_word);
}

void command_backward_word(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(backward_word);
}

void command_forward_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS([&](Contents_Iterator* it) { forward_line(buffer->mode, it); });
}

void command_backward_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS([&](Contents_Iterator* it) { backward_line(buffer->mode, it); });
}

void command_forward_line_single_cursor_visual(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    if (window->cursors.len() == 1 && !window->show_marks) {
        TRANSFORM_POINTS(
            [&](Contents_Iterator* it) { forward_visual_line(window, buffer->mode, it); });
    } else {
        TRANSFORM_POINTS([&](Contents_Iterator* it) { forward_line(buffer->mode, it); });
    }
}

void command_backward_line_single_cursor_visual(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    if (window->cursors.len() == 1 && !window->show_marks) {
        TRANSFORM_POINTS(
            [&](Contents_Iterator* it) { backward_visual_line(window, buffer->mode, it); });
    } else {
        TRANSFORM_POINTS([&](Contents_Iterator* it) { backward_line(buffer->mode, it); });
    }
}

void command_forward_paragraph(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(forward_paragraph);
}

void command_backward_paragraph(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(backward_paragraph);
}

void command_end_of_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    kill_extra_cursors(window, source.client);
    window->clear_show_marks_temporarily();
    if (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len) {
        push_jump(window, source.client, buffer);
    }
    window->cursors[0].point = buffer->contents.len;
}

void command_start_of_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    kill_extra_cursors(window, source.client);
    window->clear_show_marks_temporarily();
    if (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len) {
        push_jump(window, source.client, buffer);
    }
    window->cursors[0].point = 0;
}

void command_push_jump(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    push_jump(window, source.client, buffer);
}

void command_unpop_jump(Editor* editor, Command_Source source) {
    while (1) {
        Jump* jump = source.client->jump_chain.unpop();
        if (!jump) {
            source.client->show_message("No more jumps");
            break;
        }

        if (goto_jump(editor, source.client, jump)) {
            break;
        }

        // Invalid jump so kill it and retry.
        source.client->jump_chain.pop();
        source.client->jump_chain.kill_this_jump();
    }
}

void command_pop_jump(Editor* editor, Command_Source source) {
    if (source.client->jump_chain.index == source.client->jump_chain.jumps.len()) {
        WITH_CONST_SELECTED_BUFFER(source.client);
        push_jump(window, source.client, buffer);
        source.client->jump_chain.pop();
    }

    while (1) {
        Jump* jump = source.client->jump_chain.pop();
        if (!jump) {
            source.client->show_message("No more jumps");
            break;
        }

        if (goto_jump(editor, source.client, jump)) {
            break;
        }

        // Invalid jump so kill it and retry.
        source.client->jump_chain.kill_this_jump();
    }
}

void command_end_of_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(end_of_line);
}

void command_start_of_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(start_of_line);
}

void command_start_of_line_text(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(start_of_line_text);
}

void command_end_of_line_text(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    window->clear_show_marks_temporarily();
    TRANSFORM_POINTS(end_of_line_text);
}

void command_delete_backward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    // If temporarily showing marks then just delete the region instead.
    if (window->show_marks == 2) {
        delete_regions(source.client, buffer, window);
        window->show_marks = false;
        return;
    }

    // See if there are any cursors that are going to delete tabs.
    cz::Slice<Cursor> cursors = window->cursors;
    Contents_Iterator it = buffer->contents.start();
    size_t tabs = 0;
    for (size_t e = 0; e < cursors.len; ++e) {
        if (cursors[e].point == 0) {
            continue;
        }

        it.advance_to(cursors[e].point);

        it.retreat();
        if (it.get() == '\t') {
            ++tabs;
        }
    }

    // Treat tabs as if they were spaces when doing deletion.
    int64_t offset = 0;
    if (tabs > 0 && buffer->mode.tab_width > 1) {
        Transaction transaction;
        transaction.init(buffer);
        CZ_DEFER(transaction.drop());

        char* buf = (char*)transaction.value_allocator().alloc({buffer->mode.tab_width - 1, 1});
        memset(buf, ' ', buffer->mode.tab_width - 1);

        if (cursors.len > 1) {
            it.retreat_to(cursors[0].point);
        }
        for (size_t c = 0; c < cursors.len; ++c) {
            if (cursors[c].point == 0) {
                continue;
            }

            it.advance_to(cursors[c].point);

            it.retreat();

            // Remove the character.
            Edit remove;
            remove.position = it.position + offset;
            remove.value = SSOStr::from_char(it.get());
            remove.flags = Edit::REMOVE;
            transaction.push(remove);

            // And if it was a tab insert a bunch of spaces.
            if (it.get() == '\t') {
                uint64_t column = get_visual_column(buffer->mode, it);
                size_t len = buffer->mode.tab_width - 1 - column % buffer->mode.tab_width;

                Edit insert;
                insert.position = it.position + offset;
                insert.value = SSOStr::from_constant({buf, len});
                insert.flags = Edit::INSERT;
                transaction.push(insert);

                offset += len;
            }

            --offset;
        }

        // Don't merge edits around tab replacement.
        transaction.commit(source.client);
        return;
    }

    if (source.previous_command.function == command_delete_backward_char &&
        buffer->check_last_committer(command_delete_backward_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        CZ_DEBUG_ASSERT(commit.edits.len > 0);
        size_t len = commit.edits[commit.edits.len - 1].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            cz::Slice<Cursor> cursors = window->cursors;

            CZ_DEBUG_ASSERT(commit.edits.len == cursors.len);
            buffer->undo();
            window->update_cursors(buffer);

            Transaction transaction;
            transaction.init(buffer);
            CZ_DEFER(transaction.drop());

            uint64_t offset = 1;
            for (size_t e = 0; e < commit.edits.len; ++e) {
                if (cursors[e].point == 0) {
                    continue;
                }

                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() <= len);

                if (cursors[e].point <= len) {
                    transaction.push(commit.edits[e]);
                    continue;
                }

                Edit edit;
                memcpy(edit.value.short_._buffer + 1, commit.edits[e].value.short_._buffer, len);
                edit.value.short_._buffer[0] =
                    buffer->contents.get_once(cursors[e].point - len - 1);
                edit.value.short_.set_len(len + 1);
                edit.position = commit.edits[e].position - offset;
                offset++;
                edit.flags = Edit::REMOVE;
                transaction.push(edit);
            }

            transaction.commit(source.client, command_delete_backward_char);
            return;
        }
    }

    DELETE_BACKWARD(backward_char, command_delete_backward_char);
}

void command_delete_forward_char(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (source.previous_command.function == command_delete_forward_char &&
        buffer->check_last_committer(command_delete_forward_char, window->cursors)) {
        CZ_DEBUG_ASSERT(buffer->commit_index == buffer->commits.len());
        Commit commit = buffer->commits[buffer->commit_index - 1];
        CZ_DEBUG_ASSERT(commit.edits.len > 0);
        size_t len = commit.edits[0].value.len();
        if (len < SSOStr::MAX_SHORT_LEN) {
            cz::Slice<Cursor> cursors = window->cursors;

            CZ_DEBUG_ASSERT(commit.edits.len == cursors.len);
            buffer->undo();
            window->update_cursors(buffer);

            Transaction transaction;
            transaction.init(buffer);
            CZ_DEFER(transaction.drop());
            for (size_t e = 0; e < commit.edits.len; ++e) {
                CZ_DEBUG_ASSERT(commit.edits[e].value.is_short());
                CZ_DEBUG_ASSERT(commit.edits[e].value.len() <= len);

                if (buffer->contents.len <= cursors[e].point + len) {
                    Edit edit = commit.edits[e];
                    edit.position -= e;
                    transaction.push(edit);
                    continue;
                }

                Edit edit;
                memcpy(edit.value.short_._buffer, commit.edits[e].value.short_._buffer, len);
                edit.value.short_._buffer[len] = buffer->contents.get_once(cursors[e].point + len);
                edit.value.short_.set_len(len + 1);
                edit.position = commit.edits[e].position - e;
                edit.flags = Edit::REMOVE_AFTER_POSITION;
                transaction.push(edit);
            }
            transaction.commit(source.client, command_delete_forward_char);
            return;
        }
    }

    DELETE_FORWARD(forward_char, command_delete_forward_char);
}

void command_delete_backward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    DELETE_BACKWARD(backward_word, nullptr);
}

void command_delete_forward_word(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    DELETE_FORWARD(forward_word, nullptr);
}

void command_transpose_characters(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    for (size_t c = 0; c < cursors.len; ++c) {
        uint64_t point = cursors[c].point;
        if (point == 0 || point == buffer->contents.len) {
            continue;
        }

        Edit delete_forward;
        delete_forward.value = SSOStr::from_char(buffer->contents.get_once(point));
        delete_forward.position = point;
        delete_forward.flags = Edit::REMOVE_AFTER_POSITION;
        transaction.push(delete_forward);

        Edit insert_before;
        insert_before.value = delete_forward.value;
        insert_before.position = point - 1;
        insert_before.flags = Edit::INSERT;
        transaction.push(insert_before);
    }

    transaction.commit(source.client);
}

void command_transpose_words(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;
    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Contents_Iterator it = buffer->contents.start();
    for (size_t c = 0; c < cursors.len; ++c) {
        it.advance_to(cursors[c].point);

        Contents_Iterator start1, end1, start2, end2;

        start1 = it;
        backward_word(&start1);
        end1 = start1;
        forward_word(&end1);
        end2 = end1;
        forward_word(&end2);
        start2 = end2;
        backward_word(&start2);

        // No word to swap.
        if (start1.position == start2.position) {
            continue;
        }

        // Replace the second word.
        Edit delete2;
        delete2.value =
            buffer->contents.slice(transaction.value_allocator(), start2, end2.position);
        delete2.position = start2.position;
        delete2.flags = Edit::REMOVE_AFTER_POSITION;
        transaction.push(delete2);
        Edit insert1;
        insert1.value =
            buffer->contents.slice(transaction.value_allocator(), start1, end1.position);
        insert1.position = start2.position;
        insert1.flags = Edit::INSERT;
        transaction.push(insert1);

        // Replace the first word.
        Edit delete1;
        delete1.value = insert1.value;
        delete1.position = start1.position;
        delete1.flags = Edit::REMOVE_AFTER_POSITION;
        transaction.push(delete1);
        Edit insert2;
        insert2.value = delete2.value;
        insert2.position = start1.position;
        insert2.flags = Edit::INSERT;
        transaction.push(insert2);

        // Put the cursor after the second word.
        cursors[c].point = end2.position;
    }

    transaction.commit(source.client);
}

void command_open_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    insert_char(source.client, buffer, window, '\n');
    window->update_cursors(buffer);
    TRANSFORM_POINTS(backward_char);
}

void command_insert_newline_no_indent(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    insert_char(source.client, buffer, window, '\n');
}

void command_duplicate_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start;
        Contents_Iterator end;
        if (window->show_marks) {
            start = buffer->contents.iterator_at(cursors[c].start());
            end = buffer->contents.iterator_at(cursors[c].end());
            if (end.position > start.position) {
                end.retreat();
            }
        } else {
            start = buffer->contents.iterator_at(cursors[c].point);
            end = start;
        }
        start_of_line(&start);
        end_of_line(&end);

        size_t region_size = end.position - start.position + 1;
        char* value = (char*)transaction.value_allocator().alloc({region_size, 1});
        buffer->contents.slice_into(start, end.position, value);
        value[region_size - 1] = '\n';

        Edit edit;
        edit.value = SSOStr::from_constant({value, region_size});
        edit.position = start.position + offset;
        offset += edit.value.len();
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    transaction.commit(source.client);
}

void command_delete_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator end = start;
        start_of_line(&start);
        end_of_line(&end);
        forward_char(&end);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        edit.position = start.position - offset;
        offset += edit.value.len();
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    if (offset == 0) {
        return;
    }

    transaction.commit(source.client);
}

void command_delete_end_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator end = start;
        end_of_line(&end);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        edit.position = start.position - offset;
        offset += edit.value.len();
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    if (offset == 0) {
        return;
    }

    transaction.commit(source.client);
}

void command_delete_start_of_line_text(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator end = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator start = end;
        start_of_line_text(&start);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        edit.position = start.position - offset;
        offset += edit.value.len();
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    if (offset == 0) {
        return;
    }

    transaction.commit(source.client);
}

void command_delete_start_of_line(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t offset = 0;

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator end = buffer->contents.iterator_at(cursors[c].point);
        Contents_Iterator start = end;
        start_of_line(&start);

        Edit edit;
        edit.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        edit.position = start.position - offset;
        offset += edit.value.len();
        edit.flags = Edit::REMOVE;
        transaction.push(edit);
    }

    if (offset == 0) {
        return;
    }

    transaction.commit(source.client);
}

template <class Func>
static void fill_spaces(Client* client, Buffer* buffer, Window_Unified* window, Func func) {
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    uint64_t max_region_size = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        if (max_region_size < cursors[c].end() - cursors[c].start()) {
            max_region_size = cursors[c].end() - cursors[c].start();
        }
    }

    if (max_region_size == 0) {
        return;
    }

    cz::String spaces = {};
    spaces.reserve(transaction.value_allocator(), max_region_size);
    spaces.push_many(' ', max_region_size);

    for (size_t c = 0; c < cursors.len; ++c) {
        Contents_Iterator start, end;
        if (window->show_marks) {
            start = end = buffer->contents.iterator_at(cursors[c].end());
            start.retreat_to(cursors[c].start());
        } else {
            start = end = buffer->contents.iterator_at(cursors[c].point);
            func(&start);
            if (start.position > end.position) {
                cz::swap(start, end);
            }
        }

        Edit remove;
        remove.value = buffer->contents.slice(transaction.value_allocator(), start, end.position);
        remove.position = start.position;
        remove.flags = Edit::REMOVE;
        transaction.push(remove);

        Edit insert;
        insert.value = SSOStr::from_constant(spaces.slice_end(remove.value.len()));
        insert.position = start.position;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
    }

    transaction.commit(client);
}

void command_fill_region_with_spaces(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!window->show_marks) {
        source.client->show_message("No region selected");
        return;
    }
    fill_spaces(source.client, buffer, window,
                [](Contents_Iterator*) { CZ_PANIC("Buffer mutated while locked"); });
}

void command_fill_region_or_solt_with_spaces(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    fill_spaces(source.client, buffer, window, start_of_line_text);
}

void command_undo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->undo()) {
        source.client->show_message("Nothing to undo");
        return;
    }

    if (window->cursors.len() == 1) {
        uint64_t position = buffer->changes.last().commit.edits[0].position;
        Contents_Iterator iterator = buffer->contents.iterator_at(position);
        if (!is_visible(window, buffer->mode, iterator)) {
            window->cursors[0].point = position;
            center_in_window(window, buffer->mode, editor->theme, iterator);
        }
    }
}

void command_redo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->redo()) {
        source.client->show_message("Nothing to redo");
        return;
    }

    if (window->cursors.len() == 1) {
        uint64_t position = buffer->changes.last().commit.edits[0].position;
        Contents_Iterator iterator = buffer->contents.iterator_at(position);
        if (!is_visible(window, buffer->mode, iterator)) {
            window->cursors[0].point = position;
            center_in_window(window, buffer->mode, editor->theme, iterator);
        }
    }
}

void command_stop_action(Editor* editor, Command_Source source) {
    const char* message = nullptr;

    Window_Unified* window = source.client->selected_window();
    if (!message && window->completing) {
        window->abort_completion();
        message = "Stop completion";
    }

    if (!message && window->show_marks) {
        window->show_marks = false;
        message = "Stop selecting region";
    }

    if (!message && window->cursors.len() > 1) {
        kill_extra_cursors(window, source.client);
        message = "Stop multiple cursors";
    }

    if (!message && source.client->_select_mini_buffer) {
        source.client->hide_mini_buffer(editor);
        message = "Stop prompting";
    }

    if (!message) {
        message = "Nothing to stop";
    }

    source.client->show_message(message);
}

void command_quit(Editor* editor, Command_Source source) {
    source.client->queue_quit = true;
}

static void show_no_create_cursor_message(Editor* editor, Client* client) {
    client->show_message("No more cursors to create");
}

static void show_no_region_message(Editor* editor, Client* client) {
    client->show_message("Must select a non-empty region first");
}

static void show_created_messages(Editor* editor, Client* client, int created) {
    if (created == -1) {
        show_no_region_message(editor, client);
    }
    if (created == 0) {
        show_no_create_cursor_message(editor, client);
    }
}

static bool create_cursor_forward_line(Editor* editor,
                                       const Buffer* buffer,
                                       Window_Unified* window) {
    CZ_DEBUG_ASSERT(window->cursors.len() >= 1);
    Contents_Iterator last_cursor_iterator =
        buffer->contents.iterator_at(window->cursors.last().point);
    Contents_Iterator new_cursor_iterator = last_cursor_iterator;
    forward_line(buffer->mode, &new_cursor_iterator);
    if (new_cursor_iterator.position != last_cursor_iterator.position) {
        Cursor cursor = {};
        cursor.point = new_cursor_iterator.position;
        cursor.mark = cursor.point;
        cursor.local_copy_chain = window->cursors.last().local_copy_chain;

        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.push(cursor);
        return true;
    } else {
        return false;
    }
}

void command_create_cursor_forward_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (!create_cursor_forward_line(editor, buffer, window)) {
        show_no_create_cursor_message(editor, source.client);
    }
}

static bool create_cursor_backward_line(Editor* editor,
                                        const Buffer* buffer,
                                        Window_Unified* window) {
    CZ_DEBUG_ASSERT(window->cursors.len() >= 1);
    Contents_Iterator first_cursor_iterator =
        buffer->contents.iterator_at(window->cursors[0].point);
    Contents_Iterator new_cursor_iterator = first_cursor_iterator;
    backward_line(buffer->mode, &new_cursor_iterator);
    if (new_cursor_iterator.position != first_cursor_iterator.position) {
        Cursor cursor = {};
        cursor.point = new_cursor_iterator.position;
        cursor.mark = cursor.point;
        cursor.local_copy_chain = window->cursors[0].local_copy_chain;

        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.insert(0, cursor);
        return true;
    } else {
        return false;
    }
}

void command_create_cursor_backward_line(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (!create_cursor_backward_line(editor, buffer, window)) {
        show_no_create_cursor_message(editor, source.client);
    }
}

static bool search_forward_slice(const Buffer* buffer, Contents_Iterator* start, uint64_t end) {
    CZ_DEBUG_ASSERT(end >= start->position);
    if (end + (end - start->position) > start->contents->len) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), *start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));

    start->advance_to(end);
    return search_forward_cased(start, slice.as_str(), buffer->mode.search_continue_case_handling);
}

#define SEARCH_SLICE_THEN(FUNC, CREATED, THEN)                             \
    do {                                                                   \
        uint64_t start = cursors[c].start();                               \
        uint64_t end = cursors[c].end();                                   \
        Contents_Iterator new_start = buffer->contents.iterator_at(start); \
        (CREATED) = FUNC(buffer, &new_start, end);                         \
        if (CREATED) {                                                     \
            Cursor new_cursor = {};                                        \
            new_cursor.point = new_start.position;                         \
            new_cursor.mark = new_start.position + end - start;            \
            new_cursor.local_copy_chain = cursors[c].local_copy_chain;     \
            if (cursors[c].point > cursors[c].mark) {                      \
                cz::swap(new_cursor.point, new_cursor.mark);               \
            }                                                              \
            THEN;                                                          \
        }                                                                  \
    } while (0)

#define SEARCH_QUERY_THEN(FUNC, THEN)                                                 \
    do {                                                                              \
        uint64_t start = cursors[c].point;                                            \
        Contents_Iterator new_start = buffer->contents.iterator_at(start);            \
        for (i = 0; i < n; ++i) {                                                     \
            if (i > 0) {                                                              \
                if (FUNC == search_forward_cased) {                                   \
                    forward_char(&new_start);                                         \
                } else {                                                              \
                    backward_char(&new_start);                                        \
                }                                                                     \
            }                                                                         \
            if (!FUNC(&new_start, query, buffer->mode.search_prompt_case_handling)) { \
                break;                                                                \
            }                                                                         \
        }                                                                             \
                                                                                      \
        if (i == n) {                                                                 \
            Cursor new_cursor = {};                                                   \
            new_cursor.point = new_start.position + query.len;                        \
            new_cursor.mark = new_start.position;                                     \
            new_cursor.local_copy_chain = cursors[c].local_copy_chain;                \
            THEN;                                                                     \
        }                                                                             \
    } while (0)

static int create_cursor_forward_search(const Buffer* buffer, Window_Unified* window) {
    cz::Slice<Cursor> cursors = window->cursors;
    CZ_DEBUG_ASSERT(cursors.len >= 1);
    size_t c = cursors.len - 1;
    if (!window->show_marks || cursors[c].mark == cursors[c].point) {
        return -1;
    }
    bool created;
    SEARCH_SLICE_THEN(search_forward_slice, created, {
        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.push(new_cursor);
    });
    return created;
}

void command_create_cursor_forward_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_search(buffer, window);
    show_created_messages(editor, source.client, created);

    if (created == 1 && window->selected_cursor + 1 == window->cursors.len() - 1) {
        ++window->selected_cursor;
    }
}

static bool search_backward_slice(const Buffer* buffer, Contents_Iterator* start, uint64_t end) {
    CZ_DEBUG_ASSERT(end >= start->position);
    if (start->position < end - start->position) {
        return {};
    }

    // Optimize: don't allocate when the slice is inside one bucket
    SSOStr slice = buffer->contents.slice(cz::heap_allocator(), *start, end);
    CZ_DEFER(slice.drop(cz::heap_allocator()));

    start->retreat(end - start->position);
    return search_backward_cased(start, slice.as_str(), buffer->mode.search_continue_case_handling);
}

static int create_cursor_backward_search(const Buffer* buffer, Window_Unified* window) {
    cz::Slice<Cursor> cursors = window->cursors;
    CZ_DEBUG_ASSERT(cursors.len >= 1);
    size_t c = 0;
    if (!window->show_marks || cursors[c].mark == cursors[c].point) {
        return -1;
    }
    bool created;
    SEARCH_SLICE_THEN(search_backward_slice, created, {
        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.insert(0, new_cursor);
    });
    return created;
}

void command_create_cursor_backward_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_search(buffer, window);
    show_created_messages(editor, source.client, created);

    if (created == 1 && window->selected_cursor > 0) {
        ++window->selected_cursor;
    }
}

void command_create_cursor_forward(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created;
    if (window->show_marks) {
        created = create_cursor_forward_search(buffer, window);
    } else {
        created = create_cursor_forward_line(editor, buffer, window);
    }
    show_created_messages(editor, source.client, created);

    if (created == 1 && window->selected_cursor + 1 == window->cursors.len() - 1) {
        ++window->selected_cursor;
    }
}

void command_create_cursor_backward(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created;
    if (window->show_marks) {
        created = create_cursor_backward_search(buffer, window);
    } else {
        created = create_cursor_backward_line(editor, buffer, window);
    }
    show_created_messages(editor, source.client, created);

    if (created == 1 && window->selected_cursor > 0) {
        ++window->selected_cursor;
    }
}

void command_create_cursors_all_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_search(buffer, window);
    if (created >= 0) {
        if (created > 0) {
            ++window->selected_cursor;
            while (create_cursor_backward_search(buffer, window) == 1) {
                ++window->selected_cursor;
            }
        }
        if (create_cursor_forward_search(buffer, window) == 1) {
            created = 1;
            while (create_cursor_forward_search(buffer, window) == 1) {
            }
        }
    }

    show_created_messages(editor, source.client, created);
}

void command_create_cursors_to_end_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created = create_cursor_forward_search(buffer, window);
    if (created == 1) {
        while (create_cursor_forward_search(buffer, window) == 1) {
        }
    }
    show_created_messages(editor, source.client, created);
}

void command_create_cursors_to_start_search(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    int created = create_cursor_backward_search(buffer, window);
    if (created == 1) {
        ++window->selected_cursor;
        while (create_cursor_backward_search(buffer, window) == 1) {
            ++window->selected_cursor;
        }
    }
    show_created_messages(editor, source.client, created);
}

static void command_filter_cursors_looking_at_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str query,
                                                       void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    Contents_Iterator it = buffer->contents.start();

    // Calculate number remaining.
    size_t num_remaining = 0;
    for (size_t i = 0; i < window->cursors.len(); ++i) {
        it.advance_to(window->cursors[i].point);
        num_remaining += looking_at(it, query);
    }

    // If everything would die then handle it gracefully.
    if (num_remaining == 0) {
        kill_extra_cursors(window, client);
        return;
    }

    // Delete non-matching cursors.
    ++window->selected_cursor;
    for (size_t i = window->cursors.len(); i-- > 0;) {
        it.retreat_to(window->cursors[i].point);
        if (!looking_at(it, query)) {
            window->cursors.remove(i);
            if (window->selected_cursor > i) {
                --window->selected_cursor;
            }
        }
    }
    if (window->selected_cursor > 0) {
        --window->selected_cursor;
    }
}

static void command_filter_cursors_not_looking_at_callback(Editor* editor,
                                                           Client* client,
                                                           cz::Str query,
                                                           void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    Contents_Iterator it = buffer->contents.start();

    // Calculate number remaining.
    size_t num_remaining = 0;
    for (size_t i = 0; i < window->cursors.len(); ++i) {
        it.advance_to(window->cursors[i].point);
        num_remaining += !looking_at(it, query);
    }

    // If everything would die then handle it gracefully.
    if (num_remaining == 0) {
        kill_extra_cursors(window, client);
        return;
    }

    // Delete non-matching cursors.
    ++window->selected_cursor;
    for (size_t i = window->cursors.len(); i-- > 0;) {
        it.retreat_to(window->cursors[i].point);
        if (looking_at(it, query)) {
            window->cursors.remove(i);
            if (window->selected_cursor > i) {
                --window->selected_cursor;
            }
        }
    }
    if (window->selected_cursor > 0) {
        --window->selected_cursor;
    }
}

void command_filter_cursors_looking_at(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Filter looking at: ";
    dialog.response_callback = command_filter_cursors_looking_at_callback;
    source.client->show_dialog(dialog);
}

void command_filter_cursors_not_looking_at(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Filter not looking at: ";
    dialog.response_callback = command_filter_cursors_not_looking_at_callback;
    source.client->show_dialog(dialog);
}

static void set_cursor_position_to_edit_redo(Cursor* cursor, const Edit* edit) {
    cursor->mark = edit->position;
    cursor->point = edit->position;
    if (edit->flags & Edit::INSERT_MASK) {
        cursor->point += edit->value.len();
    }
}

static void set_cursor_position_to_edit_undo(Cursor* cursor, const Edit* edit, int64_t* offset) {
    cursor->mark = edit->position + *offset;
    cursor->point = edit->position + *offset;
    if (edit->flags & Edit::INSERT_MASK) {
        // Undo logical insert = physical remove.
        *offset -= edit->value.len();
    } else {
        // Undo logical remove = physical insert.
        cursor->point += edit->value.len();
        *offset += edit->value.len();
    }
}

static void create_cursors_last_change(Window_Unified* window,
                                       const Buffer* buffer,
                                       Client* client) {
    kill_extra_cursors(window, client);

    const Change* change = &buffer->changes.last();
    cz::Slice<const Edit> edits = change->commit.edits;
    window->cursors.reserve(cz::heap_allocator(), edits.len - 1);

    if (change->is_redo) {
        set_cursor_position_to_edit_redo(&window->cursors[0], &edits[0]);

        Copy_Chain* local_copy_chain = window->cursors[0].local_copy_chain;
        for (size_t i = 1; i < edits.len; ++i) {
            Cursor cursor = {};
            set_cursor_position_to_edit_redo(&cursor, &edits[i]);
            cursor.local_copy_chain = local_copy_chain;
            window->cursors.push(cursor);
        }
    } else {
        int64_t offset = 0;
        set_cursor_position_to_edit_undo(&window->cursors[0], &edits[0], &offset);

        Copy_Chain* local_copy_chain = window->cursors[0].local_copy_chain;
        for (size_t i = 1; i < edits.len; ++i) {
            Cursor cursor = {};
            set_cursor_position_to_edit_undo(&cursor, &edits[i], &offset);
            cursor.local_copy_chain = local_copy_chain;
            window->cursors.push(cursor);
        }
    }
}

void command_create_cursors_undo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->undo()) {
        source.client->show_message("Nothing to undo");
        return;
    }

    window->update_cursors(buffer);
    create_cursors_last_change(window, buffer, source.client);
}

void command_create_cursors_redo(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    if (!buffer->redo()) {
        source.client->show_message("Nothing to redo");
        return;
    }

    window->update_cursors(buffer);
    create_cursors_last_change(window, buffer, source.client);
}

void command_create_cursors_last_change(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (buffer->changes.len() == 0) {
        return;
    }

    create_cursors_last_change(window, buffer, source.client);
}

void command_create_cursors_lines_in_region(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    if (window->cursors.len() > 1 || !window->show_marks) {
        uint64_t start = window->cursors[0].start();
        uint64_t end = window->cursors.last().end();

        kill_extra_cursors(window, source.client);

        Contents_Iterator it = buffer->contents.iterator_at(end);
        forward_line(buffer->mode, &it);

        window->cursors[0].mark = start;
        window->cursors[0].point = it.position;
        window->show_marks = true;
        return;
    }

    Copy_Chain* local_copy_chain = window->cursors[0].local_copy_chain;

    uint64_t start = window->cursors[0].start();
    uint64_t end = window->cursors[0].end();

    Contents_Iterator iterator = buffer->contents.iterator_at(start);
    while (true) {
        forward_line(buffer->mode, &iterator);
        if (iterator.position >= end) {
            break;
        }

        Cursor cursor = {};
        cursor.point = cursor.mark = iterator.position;
        cursor.local_copy_chain = local_copy_chain;
        window->cursors.reserve(cz::heap_allocator(), 1);
        window->cursors.push(cursor);
    }

    Cursor new_cursor = {};
    new_cursor.point = new_cursor.mark = start;
    new_cursor.local_copy_chain = local_copy_chain;
    window->cursors[0] = new_cursor;

    window->show_marks = false;
}

void command_cursors_align(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;
    if (cursors.len == 1) {
        return;
    }

    Contents_Iterator iterator = buffer->contents.iterator_at(cursors[0].point);
    uint64_t min_column = 0;
    uint64_t max_column = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        uint64_t col = get_visual_column(buffer->mode, iterator);

        if (i == 0 || col > max_column) {
            max_column = col;
        }
        if (i == 0 || col < min_column) {
            min_column = col;
        }
    }

    if (max_column == min_column) {
        return;
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    char* buf = (char*)transaction.value_allocator().alloc({max_column - min_column, 1});
    CZ_DEBUG_ASSERT(buf);

    // Note: we fill with spaces as this is supposed to be used not to correctly indent lines but
    // rather to align things in those lines.  We don't want to start inserting tabs because then
    // they might look different on a different users screen.
    memset(buf, ' ', max_column - min_column);

    iterator.retreat_to(cursors[0].point);
    uint64_t offset = 0;
    for (size_t i = 0; i < cursors.len; ++i) {
        iterator.advance_to(cursors[i].point);

        uint64_t col = get_visual_column(buffer->mode, iterator);
        if (col == max_column) {
            continue;
        }

        Edit edit;
        edit.value = SSOStr::from_constant(cz::Str{buf, max_column - col});
        edit.position = cursors[i].point + offset;
        offset += max_column - col;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    transaction.commit(source.client, command_cursors_align);
}

void command_remove_cursors_at_empty_lines(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);

    size_t count_cursors = 0;
    Contents_Iterator iterator = buffer->contents.start();
    for (size_t c = 0; c < window->cursors.len(); ++c) {
        iterator.advance_to(window->cursors[c].point);
        if (at_empty_line(iterator)) {
            ++count_cursors;
        }
    }

    if (count_cursors == window->cursors.len()) {
        kill_extra_cursors(window, source.client);
        return;
    }

    iterator.go_to(window->cursors[0].point);
    for (size_t c = 0; c < window->cursors.len();) {
        iterator.advance_to(window->cursors[c].point);
        if (at_empty_line(iterator)) {
            window->cursors.remove(c);
            continue;
        }
        ++c;
    }

    if (window->cursors.len() == 1) {
        kill_extra_cursors(window, source.client);
    }
}

void command_remove_selected_cursor(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_window();

    // No cursor to kill.
    if (window->cursors.len() == 1) {
        return;
    }

    // When going down to 1 cursor we need to call `kill_extra_cursors` to cleanup various settings.
    if (window->cursors.len() == 2) {
        window->selected_cursor = 1 - window->selected_cursor;
        kill_extra_cursors(window, source.client);
        return;
    }

    // Remove the selected cursor and make sure it is still in bounds.
    window->cursors.remove(window->selected_cursor);
    if (window->selected_cursor == window->cursors.len()) {
        --window->selected_cursor;
    }
}

struct Interactive_Search_Data {
    int64_t direction;
    uint64_t cursor_point;
    uint64_t cursor_mark;
    uint64_t mini_buffer_change_index;
};

static void interactive_search_reset(Window_Unified* window, Interactive_Search_Data* data) {
    window->cursors[0].point = data->cursor_point;
    window->cursors[0].mark = data->cursor_mark;
    window->show_marks = false;
}

static void interactive_search_cancel(Editor* editor, Client* client, void* _data) {
    Interactive_Search_Data* data = (Interactive_Search_Data*)_data;
    Window_Unified* window = client->selected_normal_window;
    interactive_search_reset(window, data);
}

static void interactive_search_response_callback(Editor* editor,
                                                 Client* client,
                                                 cz::Str query,
                                                 void* _data) {
    ZoneScoped;

    Interactive_Search_Data* data = (Interactive_Search_Data*)_data;

    // If the mini buffer hasn't changed then we're already at the result.
    {
        WITH_CONST_WINDOW_BUFFER(client->_mini_buffer);
        if (data->mini_buffer_change_index == buffer->changes.len()) {
            return;
        }

        data->mini_buffer_change_index = buffer->changes.len();
    }

    Window_Unified* window = client->selected_normal_window;
    interactive_search_reset(window, data);

    WITH_CONST_WINDOW_BUFFER(window);
    cz::Slice<Cursor> cursors = window->cursors;
    size_t c = 0;
    size_t i = 0;
retry:
    if (data->direction >= 1) {
        size_t n = data->direction;
        SEARCH_QUERY_THEN(search_forward_cased, {
            cursors[0] = new_cursor;
            window->show_marks = true;
        });

        if (i == 0) {
            i = 1;
        }

        if (i != n) {
            data->direction = i;
            // Retry for case where we go off the end.
            goto retry;
        }
    } else {
        size_t n = 1 - data->direction;
        SEARCH_QUERY_THEN(search_backward_cased, {
            cursors[0] = new_cursor;
            window->show_marks = true;
        });

        if (i == 0) {
            i = 1;
        }

        if (i != n) {
            data->direction = 1 - i;
            // Retry for case where we go off the end.
            goto retry;
        }
    }
}

static void command_search_forward_callback(Editor* editor,
                                            Client* client,
                                            cz::Str query,
                                            void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);
    size_t n = 1;
    if (_data) {
        Interactive_Search_Data* data = (Interactive_Search_Data*)_data;
        interactive_search_reset(window, data);
        n = data->direction;
    }

    cz::Slice<Cursor> cursors = window->cursors;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_forward_cased, {
            // Only push jump if we hit a result, are the only cursor, and not at either sob
            // or eob.  If we don't hit a result the cursor doesn't move so push_jump just
            // adds a useless jump.  If we are at sob or eob then there is already a jump
            // and we don't want to create an intermediary jump they have top page through.
            if (window->cursors.len() == 1 && window->cursors[0].point != 0 &&
                window->cursors[0].point != buffer->contents.len) {
                push_jump(window, client, buffer);
            }

            cursors[c] = new_cursor;
            window->show_marks = true;
        });
    }
}

static void command_search_backward_callback(Editor* editor,
                                             Client* client,
                                             cz::Str query,
                                             void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);
    size_t n = 1;
    if (_data) {
        Interactive_Search_Data* data = (Interactive_Search_Data*)_data;
        interactive_search_reset(window, data);
        n = 1 - data->direction;
    }

    cz::Slice<Cursor> cursors = window->cursors;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_backward_cased, {
            // Only push jump if we hit a result, are the only cursor, and not at either sob
            // or eob.  If we don't hit a result the cursor doesn't move so push_jump just
            // adds a useless jump.  If we are at sob or eob then there is already a jump
            // and we don't want to create an intermediary jump they have top page through.
            if (window->cursors.len() == 1 && window->cursors[0].point != 0 &&
                window->cursors[0].point != buffer->contents.len) {
                push_jump(window, client, buffer);
            }

            cursors[c] = new_cursor;
            window->show_marks = true;
        });
    }
}

bool in_interactive_search(Client* client) {
    return client->_select_mini_buffer &&
           client->_message.interactive_response_callback == interactive_search_response_callback;
}

void command_search_forward(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_normal_window;

    // Already prompting.
    if (in_interactive_search(source.client)) {
        auto data = (Interactive_Search_Data*)source.client->_message.response_callback_data;
        ++data->direction;
        data->mini_buffer_change_index = 0;
        if (data->direction == 1) {
            source.client->set_prompt_text("Search forward: ");
            source.client->_message.response_callback = command_search_forward_callback;
        }
        return;
    }

    // If not interactively prompting then deal with the mini buffer.
    if (source.client->_select_mini_buffer) {
        if (source.client->_message.response_callback == command_search_backward_callback ||
            source.client->_message.response_callback == command_search_forward_callback) {
            submit_mini_buffer(editor, source.client);
        } else {
            source.client->hide_mini_buffer(editor);
        }
    }

    if (window->show_marks) {
        // Search using the matching region.
        cz::Slice<Cursor> cursors = window->cursors;
        WITH_CONST_WINDOW_BUFFER(window);
        for (size_t c = 0; c < cursors.len; ++c) {
            bool created;
            SEARCH_SLICE_THEN(search_forward_slice, created, cursors[c] = new_cursor);
        }
    } else {
        // Search using a prompt.
        Dialog dialog = {};
        dialog.prompt = "Search forward: ";
        dialog.response_callback = command_search_forward_callback;
        {
            WITH_CONST_WINDOW_BUFFER(window);
            dialog.next_token = buffer->mode.next_token;
        }

        // Interactive search.
        if (window->cursors.len() == 1) {
            Interactive_Search_Data* data = cz::heap_allocator().alloc<Interactive_Search_Data>();
            CZ_ASSERT(data);
            data->direction = 1;
            data->cursor_point = window->cursors[0].point;
            data->cursor_mark = window->cursors[0].mark;
            data->mini_buffer_change_index = 0;

            dialog.interactive_response_callback = interactive_search_response_callback;
            dialog.response_cancel = interactive_search_cancel;
            dialog.response_callback_data = data;
        }

        source.client->show_dialog(dialog);
    }
}

void command_search_backward(Editor* editor, Command_Source source) {
    Window_Unified* window = source.client->selected_normal_window;

    // Already prompting.
    if (in_interactive_search(source.client)) {
        auto data = (Interactive_Search_Data*)source.client->_message.response_callback_data;
        --data->direction;
        data->mini_buffer_change_index = 0;
        if (data->direction == 0) {
            source.client->set_prompt_text("Search backward: ");
            source.client->_message.response_callback = command_search_backward_callback;
        }
        return;
    }

    // If not interactively prompting then deal with the mini buffer.
    if (source.client->_select_mini_buffer) {
        if (source.client->_message.response_callback == command_search_backward_callback ||
            source.client->_message.response_callback == command_search_forward_callback) {
            submit_mini_buffer(editor, source.client);
        } else {
            source.client->hide_mini_buffer(editor);
        }
    }

    if (window->show_marks) {
        // Search using the matching region.
        cz::Slice<Cursor> cursors = window->cursors;
        WITH_CONST_WINDOW_BUFFER(window);
        for (size_t c = 0; c < cursors.len; ++c) {
            bool created;
            SEARCH_SLICE_THEN(search_backward_slice, created, cursors[c] = new_cursor);
        }
    } else {
        // Search using a prompt.
        Dialog dialog = {};
        dialog.prompt = "Search backward: ";
        dialog.response_callback = command_search_backward_callback;
        {
            WITH_CONST_WINDOW_BUFFER(window);
            dialog.next_token = buffer->mode.next_token;
        }

        // Interactive search.
        if (window->cursors.len() == 1) {
            Interactive_Search_Data* data = cz::heap_allocator().alloc<Interactive_Search_Data>();
            CZ_ASSERT(data);
            data->direction = 0;
            data->cursor_point = window->cursors[0].point;
            data->cursor_mark = window->cursors[0].mark;
            data->mini_buffer_change_index = 0;

            dialog.interactive_response_callback = interactive_search_response_callback;
            dialog.response_cancel = interactive_search_cancel;
            dialog.response_callback_data = data;
        }

        source.client->show_dialog(dialog);
    }
}

static void command_search_backward_expanding_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str query,
                                                       void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    cz::Slice<Cursor> cursors = window->cursors;
    if (!window->show_marks) {
        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].mark = cursors[c].point;
        }
    }

    size_t n = 1;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_backward_cased, cursors[c].point = new_cursor.mark);
    }
    window->show_marks = true;
}

static void command_search_forward_expanding_callback(Editor* editor,
                                                      Client* client,
                                                      cz::Str query,
                                                      void* _data) {
    WITH_CONST_SELECTED_BUFFER(client);

    cz::Slice<Cursor> cursors = window->cursors;
    if (!window->show_marks) {
        for (size_t c = 0; c < cursors.len; ++c) {
            cursors[c].mark = cursors[c].point;
        }
    }

    size_t n = 1;
    size_t i = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        SEARCH_QUERY_THEN(search_forward_cased, cursors[c].point = new_cursor.point);
    }
    window->show_marks = true;
}

void command_search_backward_expanding(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Search backward expanding: ";
    dialog.response_callback = command_search_backward_expanding_callback;
    {
        WITH_CONST_WINDOW_BUFFER(source.client->selected_normal_window);
        dialog.next_token = buffer->mode.next_token;
    }
    source.client->show_dialog(dialog);
}

void command_search_forward_expanding(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Search forward expanding: ";
    dialog.response_callback = command_search_forward_expanding_callback;
    {
        WITH_CONST_WINDOW_BUFFER(source.client->selected_normal_window);
        dialog.next_token = buffer->mode.next_token;
    }
    source.client->show_dialog(dialog);
}

static void parse_number(cz::Str str, uint64_t* number) {
    for (size_t i = 0; i < str.len; ++i) {
        if (!cz::is_digit(str[i])) {
            break;
        }
        *number *= 10;
        *number += str[i] - '0';
    }
}

static void command_goto_line_callback(Editor* editor, Client* client, cz::Str str, void* data) {
    uint64_t lines = 0;
    parse_number(str, &lines);

    WITH_CONST_SELECTED_BUFFER(client);
    if (window->cursors.len() > 1 ||
        (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len)) {
        push_jump(window, client, buffer);
    }

    Contents_Iterator iterator = start_of_line_position(buffer->contents, lines);
    window->cursors[0].point = iterator.position;
    center_in_window(window, buffer->mode, editor->theme, iterator);
}

static void command_goto_position_callback(Editor* editor,
                                           Client* client,
                                           cz::Str str,
                                           void* data) {
    uint64_t position = 0;
    parse_number(str, &position);

    WITH_CONST_SELECTED_BUFFER(client);
    if (window->cursors.len() > 1 ||
        (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len)) {
        push_jump(window, client, buffer);
    }

    Contents_Iterator iterator =
        buffer->contents.iterator_at(cz::min(position, buffer->contents.len));
    window->cursors[0].point = iterator.position;
    center_in_window(window, buffer->mode, editor->theme, iterator);
}

void command_goto_line(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Goto line: ";
    dialog.response_callback = command_goto_line_callback;
    source.client->show_dialog(dialog);
}

void command_goto_position(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Goto position: ";
    dialog.response_callback = command_goto_position_callback;
    source.client->show_dialog(dialog);
}

void command_path_up_directory(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (buffer->contents.len == 0) {
        return;
    }

    Contents_Iterator start = buffer->contents.iterator_at(buffer->contents.len);
    start.retreat();

    // If we find a '/' then delete after it.  Otherwise delete the entire buffer.
    if (rfind(&start, '/')) {
        start.advance();
    }

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    Edit edit;
    edit.value = buffer->contents.slice(transaction.value_allocator(), start, buffer->contents.len);
    edit.position = start.position;
    edit.flags = Edit::REMOVE;
    transaction.push(edit);

    transaction.commit(source.client);
}

void command_mark_buffer(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (window->cursors.len() > 1 ||
        (window->cursors[0].point != 0 && window->cursors[0].point != buffer->contents.len)) {
        push_jump(window, source.client, buffer);
    }
    window->show_marks = true;
    window->cursors[0].mark = 0;
    window->cursors[0].point = buffer->contents.len;
}

void submit_mini_buffer(Editor* editor, Client* client) {
    SSOStr mini_buffer_contents;
    {
        Window_Unified* window = client->mini_buffer_window();
        WITH_WINDOW_BUFFER(window);
        mini_buffer_contents = clear_buffer(client, buffer);
    }

    {
        WITH_BUFFER_HANDLE(client->messages_buffer_handle);
        buffer->contents.insert(client->_message.end, mini_buffer_contents.as_str());
    }

    client->restore_selected_buffer();

    Message message = client->_message;
    CZ_DEFER(cz::heap_allocator().dealloc({message.response_callback_data, 0}));
    client->_message.response_callback_data = nullptr;
    client->dealloc_message();

    message.response_callback(editor, client, mini_buffer_contents.as_str(),
                              message.response_callback_data);
}

void command_submit_mini_buffer(Editor* editor, Command_Source source) {
    submit_mini_buffer(editor, source.client);
}

void command_insert_home_directory(Editor* editor, Command_Source source) {
    if (!user_home_path) {
        return;
    }

    cz::Str str = user_home_path;
    size_t extra = 0;
    if (!str.ends_with("/") && !str.ends_with("\\")) {
        extra = 1;
    }

    WITH_SELECTED_BUFFER(source.client);

    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    char* buf = (char*)transaction.value_allocator().alloc({str.len + extra, 1});
    memcpy(buf, str.buffer, str.len);
    if (extra) {
        buf[str.len] = '/';
    }

    cz::path::convert_to_forward_slashes(buf, str.len);

    SSOStr ssostr = SSOStr::from_constant({buf, str.len + extra});

    uint64_t offset = 0;
    for (size_t c = 0; c < cursors.len; ++c) {
        Edit insert;
        insert.value = ssostr;
        insert.position = cursors[c].point + offset;
        insert.flags = Edit::INSERT;
        transaction.push(insert);
        offset += str.len;
    }

    transaction.commit(source.client);
}

void command_increase_font_size(Editor* editor, Command_Source source) {
    editor->theme.font_size += 2;
}

void command_decrease_font_size(Editor* editor, Command_Source source) {
    if (editor->theme.font_size >= 2) {
        editor->theme.font_size -= 2;
    }
}

void command_show_date_of_build(Editor* editor, Command_Source source) {
    char buffer[20];
    format_date(program_date, buffer);

    cz::Heap_String message = cz::format("Date of build: ", buffer);
    CZ_DEFER(message.drop());
    source.client->show_message(message);
}

void command_comment_hash(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    generic_line_comment(source.client, buffer, window, "#", /*add=*/true);
}

void command_uncomment_hash(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    generic_line_comment(source.client, buffer, window, "#", /*add=*/false);
}

static void slice_and_add_line(Contents_Iterator sol,
                               cz::Vector<SSOStr>* lines,
                               cz::Allocator allocator) {
    Contents_Iterator eol = sol;
    start_of_line(&sol);
    end_of_line(&eol);
    if (sol.position == eol.position) {
        return;
    }

    SSOStr string = sol.contents->slice(allocator, sol, eol.position);
    lines->push(string);
}

static void make_sorted_lines(cz::Vector<SSOStr>* sorted_lines,
                              cz::Slice<SSOStr> unsorted_lines,
                              int order) {
    sorted_lines->reserve(cz::heap_allocator(), unsorted_lines.len);
    sorted_lines->append(unsorted_lines);

    if (order == 0) {  // Ascending
        cz::sort(*sorted_lines,
                 [](SSOStr* left, SSOStr* right) { return left->as_str() < right->as_str(); });
    } else if (order == 1) {  // Descending
        cz::sort(*sorted_lines,
                 [](SSOStr* left, SSOStr* right) { return left->as_str() > right->as_str(); });
    } else {  // Flip
        for (size_t i = 0; i < sorted_lines->len() / 2; ++i) {
            std::swap((*sorted_lines)[i], (*sorted_lines)[sorted_lines->len() - 1 - i]);
        }
    }
}

static void set_line_to_sorted(SSOStr sorted_line,
                               SSOStr unsorted_line,
                               uint64_t position,
                               uint64_t* offset,
                               Transaction* transaction) {
    // Don't create unnecessary edits.
    if (sorted_line.as_str() == unsorted_line.as_str()) {
        return;
    }

    Edit remove_old_line;
    remove_old_line.value = unsorted_line;
    remove_old_line.position = position + *offset;
    remove_old_line.flags = Edit::REMOVE_AFTER_POSITION;
    transaction->push(remove_old_line);

    Edit insert_new_line;
    insert_new_line.value = sorted_line;
    insert_new_line.position = position + *offset;
    insert_new_line.flags = Edit::INSERT_AFTER_POSITION;
    transaction->push(insert_new_line);

    *offset += insert_new_line.value.len() - remove_old_line.value.len();
}

static void sort_lines(Client* client, Buffer* buffer, Window_Unified* window, int order) {
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::Vector<SSOStr> unsorted_lines = {};
    CZ_DEFER(unsorted_lines.drop(cz::heap_allocator()));

    cz::Vector<SSOStr> sorted_lines = {};
    CZ_DEFER(sorted_lines.drop(cz::heap_allocator()));

    Contents_Iterator it = buffer->contents.start();
    if (window->show_marks) {
        for (size_t i = 0; i < cursors.len; ++i) {
            unsorted_lines.set_len(0);
            sorted_lines.set_len(0);

            it.advance_to(cursors[i].start());
            start_of_line(&it);
            while (it.position < cursors[i].end()) {
                unsorted_lines.reserve(cz::heap_allocator(), 1);
                slice_and_add_line(it, &unsorted_lines, transaction.value_allocator());

                end_of_line(&it);
                forward_char(&it);
            }

            make_sorted_lines(&sorted_lines, unsorted_lines, order);

            it.retreat_to(cursors[i].start());
            start_of_line(&it);

            uint64_t offset = 0;
            size_t line = 0;
            while (it.position < cursors[i].end()) {
                if (!at_end_of_line(it)) {
                    set_line_to_sorted(sorted_lines[line], unsorted_lines[line], it.position,
                                       &offset, &transaction);
                    ++line;
                }

                end_of_line(&it);
                forward_char(&it);
            }
        }
    } else {
        unsorted_lines.reserve(cz::heap_allocator(), cursors.len);

        for (size_t i = 0; i < cursors.len; ++i) {
            it.advance_to(cursors[i].point);
            slice_and_add_line(it, &unsorted_lines, transaction.value_allocator());
        }

        make_sorted_lines(&sorted_lines, unsorted_lines, order);

        it.retreat_to(cursors[0].point);

        uint64_t offset = 0;
        size_t line = 0;
        for (size_t i = 0; i < cursors.len; ++i) {
            it.advance_to(cursors[i].point);
            start_of_line(&it);
            if (!at_end_of_line(it)) {
                set_line_to_sorted(sorted_lines[line], unsorted_lines[line], it.position, &offset,
                                   &transaction);
                ++line;
            }
        }
    }

    transaction.commit(client);
}

void command_sort_lines_ascending(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    sort_lines(source.client, buffer, window, 0);
}

void command_sort_lines_descending(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    sort_lines(source.client, buffer, window, 1);
}

void command_flip_lines(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    sort_lines(source.client, buffer, window, 2);
}

void command_restore_last_save_point(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);

    if (!buffer->saved_commit_id.is_present) {
        // Undo all edits.
        while (buffer->undo()) {
        }
        return;
    }

    Commit_Id saved_commit_id = buffer->saved_commit_id.value;
    for (size_t i = 0; i < buffer->commits.len(); ++i) {
        if (buffer->commits[i].id == saved_commit_id) {
            while (i < buffer->commit_index) {
                if (!buffer->undo()) {
                    return;
                }
            }
            while (i >= buffer->commit_index) {
                if (!buffer->redo()) {
                    return;
                }
            }
            return;
        }
    }
}

static void command_run_command_for_result_callback(Editor* editor,
                                                    Client* client,
                                                    cz::Str script,
                                                    void*) {
    cz::String buffer_name = {};
    CZ_DEFER(buffer_name.drop(cz::heap_allocator()));
    buffer_name.reserve(cz::heap_allocator(), 6 + script.len);
    buffer_name.append("shell ");
    buffer_name.append(script);

    WITH_CONST_SELECTED_BUFFER(client);

    run_console_command(client, editor, buffer->directory.buffer(), script, buffer_name,
                        "Shell error");
}

static void command_run_command_ignore_result_callback(Editor* editor,
                                                       Client* client,
                                                       cz::Str script,
                                                       void*) {
    WITH_CONST_SELECTED_BUFFER(client);

    cz::Process_Options options;
    options.working_directory = buffer->directory.buffer();

    cz::Process process;
    if (!process.launch_script(script, options)) {
        client->show_message("Shell error");
        return;
    }

    editor->add_asynchronous_job(job_process_silent(process));
}

void command_run_command_for_result(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Shell buffer: ";
    dialog.response_callback = command_run_command_for_result_callback;
    source.client->show_dialog(dialog);
}

void command_run_command_ignore_result(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Shell silent: ";
    dialog.response_callback = command_run_command_ignore_result_callback;
    source.client->show_dialog(dialog);
}

}
}
