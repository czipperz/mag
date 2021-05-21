#include "render.hpp"

#include <math.h>
#include <stdio.h>
#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include <cz/format.hpp>
#include <cz/sort.hpp>
#include "command_macros.hpp"
#include "decoration.hpp"
#include "diff.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "server.hpp"
#include "token.hpp"
#include "tracy_format.hpp"
#include "visible_region.hpp"

namespace mag {
namespace render {

static bool load_completion_cache(Editor* editor,
                                  Completion_Cache* completion_cache,
                                  Completion_Filter completion_filter);

#define SET_IND(FACE, CH)                                                    \
    do {                                                                     \
        Cell* cell = &cells[(y + start_row) * total_cols + (x + start_col)]; \
        cell->face = FACE;                                                   \
        cell->code = CH;                                                     \
    } while (0)

#define SET_BODY(FACE, CH)                                                                \
    do {                                                                                  \
        SET_IND(FACE, CH);                                                                \
                                                                                          \
        for (size_t spqsi = 0; spqsi < spqs.len; ++spqsi) {                               \
            if (spqs[spqsi].in_y == y + start_row && spqs[spqsi].in_x == x + start_col) { \
                spqs[spqsi].sp.found_position = true;                                     \
                spqs[spqsi].sp.position = iterator.position;                              \
            }                                                                             \
        }                                                                                 \
    } while (0)

#define ADD_NEWLINE(FACE)               \
    do {                                \
        for (; x < window->cols; ++x) { \
            SET_BODY(FACE, ' ');        \
        }                               \
        ++y;                            \
        x = 0;                          \
        if (y == window->rows) {        \
            return;                     \
        }                               \
    } while (0)

#define ADDCH(FACE, CH)                                                                       \
    do {                                                                                      \
        SET_BODY(FACE, CH);                                                                   \
        ++x;                                                                                  \
        if (x == window->cols) {                                                              \
            ADD_NEWLINE({});                                                                  \
                                                                                              \
            if (draw_line_numbers) {                                                          \
                Face face = editor->theme.special_faces[Face_Type::LINE_NUMBER_LEFT_PADDING]; \
                for (size_t i = 0; i < line_number_buffer.cap() - 1; ++i) {                   \
                    SET_BODY(face, ' ');                                                      \
                    ++x;                                                                      \
                }                                                                             \
                                                                                              \
                face = editor->theme.special_faces[Face_Type::LINE_NUMBER_RIGHT_PADDING];     \
                SET_BODY(face, ' ');                                                          \
                ++x;                                                                          \
            }                                                                                 \
        }                                                                                     \
    } while (0)

static void apply_face(Face* face, Face layer) {
    face->flags |= (layer.flags & ~Face::Flags::REVERSE);
    face->flags ^= (layer.flags & Face::Flags::REVERSE);
    if (layer.foreground != -1 && face->foreground == -1) {
        face->foreground = layer.foreground;
    }
    if (layer.background != -1 && face->background == -1) {
        face->background = layer.background;
    }
}

static Contents_Iterator update_cursors_and_run_animation(Editor* editor,
                                                          Client* client,
                                                          Window_Unified* window,
                                                          cz::Arc<Buffer_Handle> handle,
                                                          const Buffer* buffer,
                                                          Window_Cache* window_cache) {
    ZoneScoped;

    window->update_cursors(buffer);

// If we're in debug mode assert that we're sorted.  In release mode we just sort the cursors.
#ifdef NDEBUG
    cz::sort(window->cursors,
             [](const Cursor* left, const Cursor* right) { return left->point < right->point; });
#else
    CZ_DEBUG_ASSERT(cz::is_sorted(window->cursors, [](const Cursor* left, const Cursor* right) {
        return left->point < right->point;
    }));
#endif

    // Try to deal with out of bounds cursors and positions.
    if (window->start_position > buffer->contents.len) {
        window->start_position = buffer->contents.len;
    }
    if (window->cursors.last().point > buffer->contents.len) {
        kill_extra_cursors(window, client);
        window->cursors[0].point = window->cursors[0].mark = buffer->contents.len;
    }

    // Delete cursors at the same point.
    for (size_t i = 0; i + 1 < window->cursors.len();) {
        if (window->cursors[i].point == window->cursors[i + 1].point) {
            if (window->cursors.len() == 2) {
                kill_extra_cursors(window, client);
            } else {
                window->cursors.remove(i + 1);
            }
        } else {
            ++i;
        }
    }

    // Lock in write mode if the token cache is out of date.
    Buffer* buffer_mut = nullptr;
    bool token_cache_was_invalidated = false;
    if (buffer->changes.len() != buffer->token_cache.change_index) {
        buffer_mut = handle->increase_reading_to_writing();
        token_cache_was_invalidated = !buffer_mut->token_cache.update(buffer);
    }

    Contents_Iterator iterator = buffer->contents.iterator_at(window->start_position);
    start_of_line(&iterator);
    if (window_cache) {
        ZoneScopedN("update window cache");

        if (buffer->changes.len() != window_cache->v.unified.change_index) {
            position_after_changes({buffer->changes.start() + window_cache->v.unified.change_index,
                                    buffer->changes.len() - window_cache->v.unified.change_index},
                                   &window_cache->v.unified.animation.visible_start);
            position_after_changes({buffer->changes.start() + window_cache->v.unified.change_index,
                                    buffer->changes.len() - window_cache->v.unified.change_index},
                                   &window_cache->v.unified.visible_start);
            cache_window_unified_position(window, window_cache, iterator.position, buffer);
        }

        if (window->start_position != window_cache->v.unified.visible_start) {
            // The start position variable was updated in a
            // command so we recalculate the end position.
            window_cache->v.unified.visible_start = window->start_position;

            window_cache->v.unified.animation.slam_on_the_breaks = false;
        }

        // Ensure the cursor is visible
        uint64_t selected_cursor_position = window->cursors[0].point;

        Contents_Iterator second_visible_line_iterator = iterator;
        end_of_line(&second_visible_line_iterator);
        forward_char(&second_visible_line_iterator);

        Contents_Iterator visible_end_iterator =
            buffer->contents.iterator_at(window->start_position);
        forward_visible_line(buffer->mode, &visible_end_iterator, window->cols, window->rows - 2);

        if (iterator.position != 0 &&
            selected_cursor_position <= second_visible_line_iterator.position) {
            // We are above the second visible line and thus readjust
            iterator = buffer->contents.iterator_at(selected_cursor_position);
            start_of_line(&iterator);
            backward_char(&iterator);
            start_of_line(&iterator);

            // Don't readjust if the line above us is absolutely massive
            // because we just get caught in a loop going up and down.
            if (selected_cursor_position - iterator.position < (window->rows - 2) * window->cols) {
                cache_window_unified_position(window, window_cache, iterator.position, buffer);
                window_cache->v.unified.animation.slam_on_the_breaks = false;
            }
        } else if (selected_cursor_position > visible_end_iterator.position) {
            // We are below the "visible" section of the buffer ie on the last line or beyond
            // the last line.
            iterator = buffer->contents.iterator_at(selected_cursor_position);
            start_of_line(&iterator);
            backward_visible_line(buffer->mode, &iterator, window->cols, window->rows - 2);
            cache_window_unified_position(window, window_cache, iterator.position, buffer);

            window_cache->v.unified.animation.slam_on_the_breaks = false;
        }

        // Test if we can fit any of the newly created cursors on the screen.
        if (window->cursors.len() > window_cache->v.unified.cursor_count) {
            for (size_t c = window->cursors.len(); c-- > window_cache->v.unified.cursor_count;) {
                // Test if we can put this cursor on the screen
                // without bumping the primary cursor off.
                Contents_Iterator start_iterator =
                    buffer->contents.iterator_at(window->cursors[c].point);
                start_of_line(&start_iterator);
                backward_visible_line(buffer->mode, &start_iterator, window->cols,
                                      window->rows - 1);

                Contents_Iterator test_iterator = start_iterator;
                end_of_line(&test_iterator);
                forward_char(&test_iterator);

                if (window->cursors[0].point >= test_iterator.position &&
                    start_iterator.position > iterator.position) {
                    iterator = start_iterator;
                    cache_window_unified_position(window, window_cache, iterator.position, buffer);
                    window_cache->v.unified.animation.slam_on_the_breaks = false;
                    break;
                }
            }
        }
        window_cache->v.unified.cursor_count = window->cursors.len();

        float speed_start = 0.5f;
        float speed_increment = 0.5f;
        float speed_multiplier = 1.4f;

        // When accelerating we preincrement the speed.  So when we break we need to
        // postdecrement the speed.  The way we do this is by storing the original
        // speed and then overriding it if we are accelerating with the new speed.
        float speed_lines_to_shift = window_cache->v.unified.animation.speed;

        // Setup animation.
        if (window_cache->v.unified.animation.slam_on_the_breaks) {
            if (window_cache->v.unified.animation.speed > 0) {
                window_cache->v.unified.animation.speed -= speed_increment;
                window_cache->v.unified.animation.speed /= speed_multiplier;
                window_cache->v.unified.animation.speed =
                    std::max(window_cache->v.unified.animation.speed, 1.0f);
            } else {
                window_cache->v.unified.animation.speed += speed_increment;
                window_cache->v.unified.animation.speed /= speed_multiplier;
                window_cache->v.unified.animation.speed =
                    std::min(window_cache->v.unified.animation.speed, -1.0f);
            }
        } else if (window_cache->v.unified.animation.visible_start < iterator.position) {
            if (window_cache->v.unified.animation.speed <= 0) {
                window_cache->v.unified.animation.speed = speed_start;
            }
            window_cache->v.unified.animation.speed *= speed_multiplier;
            window_cache->v.unified.animation.speed += speed_increment;
            if (window_cache->v.unified.animation.speed > (float)window->rows) {
                window_cache->v.unified.animation.speed = (float)window->rows;
            }
            speed_lines_to_shift = window_cache->v.unified.animation.speed;
        } else if (window_cache->v.unified.animation.visible_start > iterator.position) {
            if (window_cache->v.unified.animation.speed >= 0) {
                window_cache->v.unified.animation.speed = -speed_start;
            }
            window_cache->v.unified.animation.speed *= speed_multiplier;
            window_cache->v.unified.animation.speed -= speed_increment;
            if (window_cache->v.unified.animation.speed < -(float)window->rows) {
                window_cache->v.unified.animation.speed = -(float)window->rows;
            }
            speed_lines_to_shift = window_cache->v.unified.animation.speed;
        }

        // Run animations.
        if (window_cache->v.unified.animation.speed != 0) {
            ZoneScopedN("run animation");

            // If we are out of bounds because the user directly modified the `Contents`
            // without making a `Change` then go back into bounds so we don't crash.
            if (window_cache->v.unified.animation.visible_start > buffer->contents.len) {
                window_cache->v.unified.animation.visible_start = buffer->contents.len;
            }

            iterator.go_to(window_cache->v.unified.animation.visible_start);

            if (window_cache->v.unified.animation.speed < 0) {
                // If we're within one page and over half way there then start breaking.
                Contents_Iterator end_iterator = iterator;
                end_iterator.retreat_to(window->start_position);
                forward_visible_line(buffer->mode, &end_iterator, window->cols, window->rows - 1);

                // Tokenization happens from the top of the file to the bottom.  So if we want to
                // move from the bottom to the top and the bottom isn't tokenized then we would
                // stall the editor.  So jump to one page from the end and animate from there.
                bool force_teleport = false;
                if (!buffer->token_cache.is_covered(
                        window_cache->v.unified.animation.visible_start)) {
                    force_teleport = true;
                    window_cache->v.unified.animation.visible_start = end_iterator.position;
                }

                bool force_break = false;
                float speed_loop_speed = speed_start;
                if (!window_cache->v.unified.animation.slam_on_the_breaks &&
                    (force_teleport || iterator.position <= end_iterator.position ||
                     window_cache->v.unified.animation.speed <= -(float)window->rows)) {
                    size_t lines = 0;
                    Contents_Iterator it = iterator;
                    it.retreat_to(window->start_position);
                    while (it.position < end_iterator.position) {
                        end_of_line(&it);
                        forward_char(&it);
                        ++lines;
                    }

                    float distance = 0;
                    while (1) {
                        speed_loop_speed *= speed_multiplier;
                        speed_loop_speed += speed_increment;
                        distance += ceilf(speed_loop_speed);
                        if (distance >= lines) {
                            break;
                        }
                    }

                    force_break = speed_loop_speed <= -window_cache->v.unified.animation.speed;
                }

                // If we are already breaking or are moving too
                // slow to teleport, then move line by line.
                if (window_cache->v.unified.animation.slam_on_the_breaks ||
                    (!force_break && !force_teleport &&
                     window_cache->v.unified.animation.speed > -(float)window->rows)) {
                    // Go line by line.
                    for (float i = -speed_lines_to_shift; i > 0; --i) {
                        backward_char(&iterator);
                        start_of_line(&iterator);
                    }
                } else {
                    // Teleport almost all the way there and start breaking.
                    window_cache->v.unified.animation.slam_on_the_breaks = true;
                    window_cache->v.unified.animation.speed = -speed_loop_speed;

                    if (end_iterator.position >= iterator.position) {
                        end_iterator.retreat_to(window->start_position);
                    }
                    iterator = end_iterator;
                }

                CZ_DEBUG_ASSERT(window->start_position == window_cache->v.unified.visible_start);
                if (window_cache->v.unified.visible_start >= iterator.position) {
                    iterator.advance_to(window_cache->v.unified.visible_start);
                    window_cache->v.unified.animation.speed = 0;
                    window_cache->v.unified.animation.slam_on_the_breaks = false;
                }
            }

            if (window_cache->v.unified.animation.speed > 0) {
                // If we're within one page and over half way there then start breaking.
                Contents_Iterator start_iterator = iterator;
                start_iterator.advance_to(window->start_position);
                backward_visible_line(buffer->mode, &start_iterator, window->cols,
                                      window->rows - 1);

                bool force_break = false;
                float speed_loop_speed = speed_start;
                if (!window_cache->v.unified.animation.slam_on_the_breaks &&
                    (iterator.position >= start_iterator.position ||
                     window_cache->v.unified.animation.speed >= (float)window->rows)) {
                    size_t lines = 1;
                    Contents_Iterator it = iterator;
                    it.advance_to(window->start_position);
                    while (it.position > start_iterator.position) {
                        backward_char(&it);
                        start_of_line(&it);
                        ++lines;
                    }

                    float distance = 0;
                    while (1) {
                        speed_loop_speed *= speed_multiplier;
                        speed_loop_speed += speed_increment;
                        distance += ceilf(speed_loop_speed);
                        if (distance >= lines) {
                            break;
                        }
                    }

                    force_break = speed_loop_speed <= window_cache->v.unified.animation.speed;
                }

                // If we are already breaking, haven't tokenized to leap all the way without
                // stalling, or are moving too slow to teleport, then move line by line.
                if (window_cache->v.unified.animation.slam_on_the_breaks ||
                    !buffer->token_cache.is_covered(window->start_position) ||
                    (!force_break &&
                     window_cache->v.unified.animation.speed < (float)window->rows)) {
                    // Go line by line.
                    for (float i = speed_lines_to_shift; i > 0; --i) {
                        end_of_line(&iterator);
                        forward_char(&iterator);
                    }
                } else {
                    // Teleport almost all the way there and start breaking.
                    window_cache->v.unified.animation.slam_on_the_breaks = true;
                    window_cache->v.unified.animation.speed = speed_loop_speed;

                    if (start_iterator.position <= iterator.position) {
                        start_iterator.advance_to(window->start_position);
                    }
                    iterator = start_iterator;
                }

                CZ_DEBUG_ASSERT(window->start_position == window_cache->v.unified.visible_start);
                if (window_cache->v.unified.visible_start <= iterator.position) {
                    iterator.retreat_to(window_cache->v.unified.visible_start);
                    window_cache->v.unified.animation.speed = 0;
                    window_cache->v.unified.animation.slam_on_the_breaks = false;
                }
            }

            window_cache->v.unified.animation.visible_start = iterator.position;
        }
    } else {
        window->start_position = iterator.position;
    }

    // If we moved into an area that isn't covered then we need to generate check points.
    if (!buffer_mut && !buffer->token_cache.is_covered(iterator.position)) {
        buffer_mut = handle->increase_reading_to_writing();
    }

    if (buffer_mut) {
        // Note: we update the token cache at the top of this function.
        CZ_DEBUG_ASSERT(buffer->token_cache.change_index == buffer->changes.len());

        // Cover the visible region with check points.
        bool had_no_check_points = buffer->token_cache.check_points.len() == 0;
        buffer_mut->token_cache.generate_check_points_until(buffer, iterator.position);

        if (token_cache_was_invalidated || had_no_check_points) {
            // Start asynchronous syntax highlighting.
            TracyFormat(message, len, 1024, "Start syntax highlighting: %.*s",
                        (int)buffer->name.len(), buffer->name.buffer());
            TracyMessage(message, len);
            editor->add_asynchronous_job(job_syntax_highlight_buffer(handle.clone_downgrade()));
        }

        // Unlock writing.
        handle->reduce_writing_to_reading();
    }

    return iterator;
}

static void draw_buffer_contents(Cell* cells,
                                 Window_Cache* window_cache,
                                 size_t total_cols,
                                 Editor* editor,
                                 Client* client,
                                 const Buffer* buffer,
                                 Window_Unified* window,
                                 size_t start_row,
                                 size_t start_col,
                                 cz::Slice<Screen_Position_Query> spqs,
                                 size_t* cursor_pos_y,
                                 size_t* cursor_pos_x,
                                 Contents_Iterator iterator) {
    ZoneScoped;

    size_t y = 0;
    size_t x = 0;

    Token token = {};
    uint64_t state = 0;
    bool has_token = true;
    // Note: we run `buffer->token_cache.update(buffer)` in update_cursors_and_run_animation.
    CZ_DEBUG_ASSERT(buffer->token_cache.change_index == buffer->changes.len());
    {
        Tokenizer_Check_Point check_point;
        if (buffer->token_cache.find_check_point(iterator.position, &check_point)) {
            token.end = check_point.position;
            state = check_point.state;
        }
    }

    cz::Slice<Cursor> cursors = window->cursors;

    int show_mark = 0;
    // Initialize show_mark to number of regions at start of visible region.
    if (window->show_marks) {
        for (size_t c = 0; c < cursors.len; ++c) {
            if (iterator.position > cursors[c].start() && iterator.position < cursors[c].end()) {
                ++show_mark;
            }
        }
    }

    CZ_DEFER({
        for (size_t i = 0; i < editor->theme.overlays.len(); ++i) {
            const Overlay* overlay = &editor->theme.overlays[i];
            overlay->end_frame();
        }
        for (size_t i = 0; i < buffer->mode.overlays.len(); ++i) {
            const Overlay* overlay = &buffer->mode.overlays[i];
            overlay->end_frame();
        }
    });

    for (size_t i = 0; i < editor->theme.overlays.len(); ++i) {
        const Overlay* overlay = &editor->theme.overlays[i];
        overlay->start_frame(buffer, window, iterator);
    }
    for (size_t i = 0; i < buffer->mode.overlays.len(); ++i) {
        const Overlay* overlay = &buffer->mode.overlays[i];
        overlay->start_frame(buffer, window, iterator);
    }

    uint64_t line_number = buffer->contents.get_line_number(iterator.position) + 1;
    cz::String line_number_buffer = {};
    CZ_DEFER(line_number_buffer.drop(cz::heap_allocator()));
    {
        Contents_Iterator end_position = iterator;
        forward_visible_line(buffer->mode, &end_position, window->cols, window->rows);

        size_t end_line_number = buffer->contents.get_line_number(end_position.position) + 1;
        size_t line_number_width = log10(end_line_number) + 1;
        line_number_buffer.reserve(cz::heap_allocator(), line_number_width + 1);
    }

    // Draw line number for first line.
    bool draw_line_numbers =
        editor->theme.draw_line_numbers && line_number_buffer.cap() + 5 <= window->cols;
    if (draw_line_numbers) {
        int ret = snprintf(line_number_buffer.buffer(), line_number_buffer.cap(), "%*zu",
                           (int)(line_number_buffer.cap() - 1), line_number);
        if (ret > 0) {
            line_number_buffer.set_len(ret);

            size_t i = 0;
            Face face = editor->theme.special_faces[Face_Type::LINE_NUMBER_LEFT_PADDING];
            for (; i < line_number_buffer.cap() - 1; ++i) {
                char ch = line_number_buffer[i];
                if (ch != ' ') {
                    break;
                }
                SET_BODY(face, ch);
                ++x;
            }

            face = editor->theme.special_faces[Face_Type::LINE_NUMBER];
            for (; i < line_number_buffer.cap() - 1; ++i) {
                char ch = line_number_buffer[i];
                SET_BODY(face, ch);
                ++x;
            }

            face = editor->theme.special_faces[Face_Type::LINE_NUMBER_RIGHT_PADDING];
            ADDCH(face, ' ');
        }
    }

    Contents_Iterator token_iterator = buffer->contents.iterator_at(token.end);
    for (; !iterator.at_eob(); iterator.advance()) {
        while (has_token && iterator.position >= token.end) {
            has_token = buffer->mode.next_token(&token_iterator, &token, &state);
        }

        bool has_cursor = false;
        for (size_t c = 0; c < cursors.len; ++c) {
            if (window->show_marks) {
                if (iterator.position == cursors[c].start()) {
                    ++show_mark;
                }
                if (iterator.position == cursors[c].end()) {
                    --show_mark;
                }
            }
            if (iterator.position == cursors[c].point) {
                has_cursor = true;
                if (c == 0) {
                    *cursor_pos_y = y;
                    *cursor_pos_x = x;
                }
            }
        }

#if 0
        if (iterator.index == 0) {
            Face face = {};
            face.background = {1};
            ADDCH(face, '\'');
        }
#endif

        Face face = {};
        if (has_cursor) {
            apply_face(&face, editor->theme.special_faces[Face_Type::CURSOR]);
        }

        if (show_mark) {
            apply_face(&face, editor->theme.special_faces[Face_Type::MARKED_REGION]);
        }

        {
            size_t j = 0;
            for (size_t i = 0; i < editor->theme.overlays.len(); ++i, ++j) {
                const Overlay* overlay = &editor->theme.overlays[i];
                Face overlay_face = overlay->get_face_and_advance(buffer, window, iterator);
                apply_face(&face, overlay_face);
            }
            for (size_t i = 0; i < buffer->mode.overlays.len(); ++i, ++j) {
                const Overlay* overlay = &buffer->mode.overlays[i];
                Face overlay_face = overlay->get_face_and_advance(buffer, window, iterator);
                apply_face(&face, overlay_face);
            }
        }

        Face token_face;
        if (has_token && iterator.position >= token.start && iterator.position < token.end) {
            if (token.type & Token_Type::CUSTOM) {
                token_face = Token_Type_::decode(token.type);
            } else {
                token_face = editor->theme.token_faces[token.type];
            }
        } else {
            token_face = editor->theme.token_faces[Token_Type::DEFAULT];
        }
        apply_face(&face, token_face);

        if (face.flags & Face::INVISIBLE) {
            // Skip rendering this character as it is invisible
            continue;
        }

        char ch = iterator.get();
        if (ch == '\n') {
            SET_BODY(face, ' ');
            ++x;

            // Draw newline padding with faces from overlays
            {
                Face face;
                size_t j = 0;
                for (size_t i = 0; i < editor->theme.overlays.len(); ++i, ++j) {
                    const Overlay* overlay = &editor->theme.overlays[i];
                    Face overlay_face = overlay->get_face_newline_padding(buffer, window, iterator);
                    apply_face(&face, overlay_face);
                }
                for (size_t i = 0; i < buffer->mode.overlays.len(); ++i, ++j) {
                    const Overlay* overlay = &buffer->mode.overlays[i];
                    Face overlay_face = overlay->get_face_newline_padding(buffer, window, iterator);
                    apply_face(&face, overlay_face);
                }
                ADD_NEWLINE(face);
            }

            // Draw line number.  Note the first line number is drawn before the loop.
            if (draw_line_numbers) {
                line_number += 1;
                int ret = snprintf(line_number_buffer.buffer(), line_number_buffer.cap(), "%*zu",
                                   (int)(line_number_buffer.cap() - 1), line_number);
                if (ret > 0) {
                    line_number_buffer.set_len(ret);

                    size_t i = 0;
                    Face face = editor->theme.special_faces[Face_Type::LINE_NUMBER_LEFT_PADDING];
                    for (; i < line_number_buffer.cap() - 1; ++i) {
                        char ch = line_number_buffer[i];
                        if (ch != ' ') {
                            break;
                        }
                        SET_BODY(face, ch);
                        ++x;
                    }

                    face = editor->theme.special_faces[Face_Type::LINE_NUMBER];
                    for (; i < line_number_buffer.cap() - 1; ++i) {
                        char ch = line_number_buffer[i];
                        SET_BODY(face, ch);
                        ++x;
                    }

                    face = editor->theme.special_faces[Face_Type::LINE_NUMBER_RIGHT_PADDING];
                    ADDCH(face, ' ');
                }
            }
        } else if (ch == '\t') {
            size_t line_number_offset = 0;
            if (draw_line_numbers) {
                line_number_offset = line_number_buffer.cap();
            }

            size_t end_x = x + buffer->mode.tab_width;
            end_x -= line_number_offset;
            end_x -= end_x % buffer->mode.tab_width;
            end_x += line_number_offset;
            for (size_t i = x; i < end_x; ++i) {
                ADDCH(face, ' ');
            }
        } else if (cz::is_print(ch)) {
            ADDCH(face, ch);
        } else {
            ADDCH(face, '\\');
            ADDCH(face, '[');
            bool already = false;
            if ((ch / 100) % 10) {
                ADDCH(face, (ch / 100) % 10 + '0');
                already = true;
            }
            if ((ch / 10) % 10 || already) {
                ADDCH(face, (ch / 10) % 10 + '0');
            }
            ADDCH(face, ch % 10 + '0');
            ADDCH(face, ';');
        }
    }

    {
        // Draw cursor at end of file.
        Face face = {};
        apply_face(&face, editor->theme.special_faces[Face_Type::CURSOR]);
        for (size_t c = 0; c < cursors.len; ++c) {
            if (cursors[c].point == buffer->contents.len) {
                SET_BODY(face, ' ');
                ++x;
                break;
            }
        }
    }

    for (; y < window->rows; ++y) {
        for (; x < window->cols; ++x) {
            SET_BODY({}, ' ');
        }
        x = 0;
    }
}

static void draw_buffer_decoration(Cell* cells,
                                   size_t total_cols,
                                   Editor* editor,
                                   Window_Unified* window,
                                   const Buffer* buffer,
                                   bool is_selected_window,
                                   size_t start_row,
                                   size_t start_col) {
    ZoneScoped;

    size_t y = window->rows;
    size_t x = 0;

    Face face = {};
    if (!buffer->is_unchanged()) {
        apply_face(&face, editor->theme.special_faces[Face_Type::UNSAVED_MODE_LINE]);
    }
    if (is_selected_window) {
        apply_face(&face, editor->theme.special_faces[Face_Type::SELECTED_MODE_LINE]);
    }
    apply_face(&face, editor->theme.special_faces[Face_Type::DEFAULT_MODE_LINE]);

    cz::Heap_String string = {};
    CZ_DEFER(string.drop());
    string.reserve(1024);
    buffer->render_name(cz::heap_allocator(), &string);

    cz::append(&string, ' ');

    for (size_t i = 0; i < editor->theme.decorations.len(); ++i) {
        if (editor->theme.decorations[i].append(buffer, window, cz::heap_allocator(), &string)) {
            cz::append(&string, ' ');
        }
    }
    for (size_t i = 0; i < buffer->mode.decorations.len(); ++i) {
        if (buffer->mode.decorations[i].append(buffer, window, cz::heap_allocator(), &string)) {
            cz::append(&string, ' ');
        }
    }

    size_t max = cz::min<size_t>(string.len(), window->cols - x);
    for (size_t i = 0; i < max; ++i) {
        SET_IND(face, string[i]);
        ++x;
    }
    for (; x < window->cols; ++x) {
        SET_IND(face, ' ');
    }
}

static void draw_window_completion(Cell* cells,
                                   Editor* editor,
                                   Client* client,
                                   Window_Unified* window,
                                   const Buffer* buffer,
                                   size_t total_cols,
                                   size_t start_row,
                                   size_t start_col,
                                   size_t cursor_pos_y,
                                   size_t cursor_pos_x) {
    if (!window->completing) {
        return;
    }
    window->update_completion_cache(buffer);
    if (!window->completing) {
        return;
    }

    load_completion_cache(editor, &window->completion_cache,
                          editor->theme.window_completion_filter);
    if (window->completion_cache.filter_context.results.len() == 0) {
        client->show_message(editor, "No completion results");
        window->abort_completion();
        return;
    }

    if (window->completion_cache.state == Completion_Cache::LOADED) {
        // todo: deduplicate with the code to draw mini buffer completion cache
        size_t height = window->completion_cache.filter_context.results.len();
        if (height > editor->theme.max_completion_results) {
            height = editor->theme.max_completion_results;
        }
        if (height > window->rows / 2) {
            height = window->rows / 2;
        }

        size_t offset = window->completion_cache.filter_context.selected;
        if (offset >= window->completion_cache.filter_context.results.len() - height / 2) {
            offset = window->completion_cache.filter_context.results.len() - height;
        } else if (offset < height / 2) {
            offset = 0;
        } else {
            offset -= height / 2;
        }

        size_t width = 0;
        for (size_t r = offset; r < height + offset; ++r) {
            if (window->completion_cache.filter_context.results[r].len > width) {
                width = window->completion_cache.filter_context.results[r].len;
            }
        }

        size_t y = cursor_pos_y;
        size_t x = cursor_pos_x;

        bool narrow_line = false;
        if (x + width > window->cols) {
            if (window->cols >= width) {
                x = window->cols - width;
            } else {
                x = 0;
                width = window->cols;
            }
            narrow_line = true;
        }

        size_t start_x = x;

        size_t lines_above = cursor_pos_y + !narrow_line;
        size_t lines_below = window->rows - cursor_pos_y - narrow_line;
        if (narrow_line + height > lines_below) {
            if (lines_above > lines_below) {
                y = cursor_pos_y - height + !narrow_line;
            } else {
                height = lines_below;
            }
        } else {
            y += narrow_line;
        }

        for (size_t r = offset; r < height + offset; ++r) {
            Face face = {};
            if (r == window->completion_cache.filter_context.selected) {
                apply_face(&face,
                           editor->theme.special_faces[Face_Type::WINDOW_COMPLETION_SELECTED]);
            } else {
                apply_face(&face, editor->theme.special_faces[Face_Type::WINDOW_COMPLETION_NORMAL]);
            }

            cz::Str result = window->completion_cache.filter_context.results[r];
            for (size_t i = 0; i < width && i < result.len; ++i) {
                SET_IND(face, result[i]);
                ++x;
            }

            for (; x < start_x + width; ++x) {
                SET_IND(face, ' ');
            }

            x = start_x;
            ++y;
        }
    }
}

static void draw_buffer(Cell* cells,
                        Window_Cache* window_cache,
                        size_t total_cols,
                        Editor* editor,
                        Client* client,
                        Window_Unified* window,
                        bool is_selected_window,
                        size_t start_row,
                        size_t start_col,
                        cz::Slice<Screen_Position_Query> spqs) {
    ZoneScoped;

    for (size_t spqsi = 0; spqsi < spqs.len; ++spqsi) {
        if (start_col <= spqs[spqsi].in_x && spqs[spqsi].in_x < start_col + window->cols &&
            start_row <= spqs[spqsi].in_y && spqs[spqsi].in_y < start_row + window->rows) {
            spqs[spqsi].sp.found_window = true;
            spqs[spqsi].sp.window = window;
            spqs[spqsi].sp.window_row = spqs[spqsi].in_y - start_row;
            spqs[spqsi].sp.window_column = spqs[spqsi].in_x - start_col;
        }
    }

    WITH_CONST_BUFFER(window->id);

    Contents_Iterator iterator =
        update_cursors_and_run_animation(editor, client, window, handle, buffer, window_cache);

    size_t cursor_pos_y, cursor_pos_x;
    draw_buffer_contents(cells, window_cache, total_cols, editor, client, buffer, window, start_row,
                         start_col, spqs, &cursor_pos_y, &cursor_pos_x, iterator);
    draw_buffer_decoration(cells, total_cols, editor, window, buffer, is_selected_window, start_row,
                           start_col);
    draw_window_completion(cells, editor, client, window, buffer, total_cols, start_row, start_col,
                           cursor_pos_y, cursor_pos_x);
}

static void draw_window(Cell* cells,
                        Window_Cache** window_cache,
                        size_t total_cols,
                        Editor* editor,
                        Client* client,
                        Window* w,
                        Window* selected_window,
                        size_t start_row,
                        size_t start_col,
                        size_t count_rows,
                        size_t count_cols,
                        cz::Slice<Screen_Position_Query> spqs) {
    ZoneScoped;

    w->rows = count_rows;
    w->cols = count_cols;

    switch (w->tag) {
    case Window::UNIFIED: {
        Window_Unified* window = (Window_Unified*)w;

        --window->rows;

        if (!*window_cache) {
            *window_cache = cz::heap_allocator().alloc<Window_Cache>();
            CZ_ASSERT(*window_cache);
            cache_window_unified_create(editor, *window_cache, window);
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            cache_window_unified_create(editor, *window_cache, window);
        } else if ((*window_cache)->v.unified.id != window->id) {
            cache_window_unified_create(editor, *window_cache, window);
        }

        draw_buffer(cells, *window_cache, total_cols, editor, client, window,
                    window == selected_window, start_row, start_col, spqs);
        break;
    }

    case Window::VERTICAL_SPLIT:
    case Window::HORIZONTAL_SPLIT: {
        Window_Split* window = (Window_Split*)w;

        if (!*window_cache) {
            *window_cache = cz::heap_allocator().alloc<Window_Cache>();
            CZ_ASSERT(*window_cache);
            (*window_cache)->tag = window->tag;
            (*window_cache)->v.split = {};
        } else if ((*window_cache)->tag != window->tag) {
            destroy_window_cache_children(*window_cache);
            (*window_cache)->tag = window->tag;
            (*window_cache)->v.split = {};
        }

        if (window->tag == Window::VERTICAL_SPLIT) {
            size_t left_cols = (count_cols - 1) * window->split_ratio;
            size_t right_cols = count_cols - left_cols - 1;

            draw_window(cells, &(*window_cache)->v.split.first, total_cols, editor, client,
                        window->first, selected_window, start_row, start_col, count_rows, left_cols,
                        spqs);

            {
                size_t x = left_cols;
                for (size_t y = 0; y < count_rows; ++y) {
                    SET_IND({}, '|');
                }
            }

            draw_window(cells, &(*window_cache)->v.split.second, total_cols, editor, client,
                        window->second, selected_window, start_row,
                        start_col + count_cols - right_cols, count_rows, right_cols, spqs);
        } else {
            size_t top_rows = (count_rows - 1) * window->split_ratio;
            size_t bottom_rows = count_rows - top_rows - 1;

            draw_window(cells, &(*window_cache)->v.split.first, total_cols, editor, client,
                        window->first, selected_window, start_row, start_col, top_rows, count_cols,
                        spqs);

            {
                size_t y = top_rows;
                for (size_t x = 0; x < count_cols; ++x) {
                    SET_IND({}, '-');
                }
            }

            draw_window(cells, &(*window_cache)->v.split.second, total_cols, editor, client,
                        window->second, selected_window, start_row + count_rows - bottom_rows,
                        start_col, bottom_rows, count_cols, spqs);
        }
        break;
    }
    }
}

bool load_mini_buffer_completion_cache(Server* server, Client* client) {
    ZoneScoped;

    if (client->_message.tag <= Message::SHOW) {
        return false;
    }

    return load_completion_cache(&server->editor, &client->mini_buffer_completion_cache,
                                 server->editor.theme.mini_buffer_completion_filter);
}

static bool load_completion_cache(Editor* editor,
                                  Completion_Cache* completion_cache,
                                  Completion_Filter completion_filter) {
    CZ_DEBUG_ASSERT(completion_filter != nullptr);

    cz::String selected_result = {};
    CZ_DEFER(selected_result.drop(cz::heap_allocator()));
    bool has_selected_result = false;
    if (completion_cache->filter_context.selected <
        completion_cache->filter_context.results.len()) {
        cz::Str selected_result_str =
            completion_cache->filter_context.results[completion_cache->filter_context.selected];
        selected_result.reserve(cz::heap_allocator(), selected_result_str.len);
        selected_result.append(selected_result_str);
        has_selected_result = true;
    }

    bool engine_change =
        completion_cache->engine(editor, &completion_cache->engine_context,
                                 completion_cache->state == Completion_Cache::INITIAL);

    if (completion_cache->state != Completion_Cache::LOADED || engine_change) {
        completion_cache->filter_context.selected = 0;
        completion_cache->filter_context.results.set_len(0);
        completion_cache->filter_context.results.reserve(
            completion_cache->engine_context.results.len());
        completion_filter(editor, &completion_cache->filter_context,
                          &completion_cache->engine_context, selected_result, has_selected_result);
    }

    completion_cache->state = Completion_Cache::LOADED;
    return true;
}

void process_buffer_external_updates(Editor* editor, Client* client, Window* window) {
    ZoneScoped;

    switch (window->tag) {
    case Window::UNIFIED: {
        Window_Unified* w = (Window_Unified*)window;
        WITH_CONST_WINDOW_BUFFER(w);

        if (!buffer->is_unchanged() || !buffer->has_file_time) {
            return;
        }

        cz::String path = {};
        CZ_DEFER(path.drop(cz::heap_allocator()));
        if (!buffer->get_path(cz::heap_allocator(), &path)) {
            return;
        }

        cz::File_Time file_time = buffer->file_time;
        if (check_out_of_date_and_update_file_time(path.buffer(), &file_time)) {
            Buffer* buffer_mut = handle->increase_reading_to_writing();
            buffer_mut->file_time = file_time;
            reload_file(editor, client, buffer_mut);
        }

        break;
    }

    default: {
        auto w = (Window_Split*)window;
        process_buffer_external_updates(editor, client, w->first);
        process_buffer_external_updates(editor, client, w->second);
        break;
    }
    }
}

void render_to_cells(Cell* cells,
                     Window_Cache** window_cache,
                     size_t total_rows,
                     size_t total_cols,
                     Editor* editor,
                     Client* client,
                     cz::Slice<Screen_Position_Query> spqs) {
    ZoneScoped;

    size_t mini_buffer_height = 0;

    if (client->_message.tag != Message::NONE) {
        ZoneScopedN("Draw mini buffer");

        mini_buffer_height = 1;
        size_t results_height = 0;

        Completion_Cache* completion_cache = &client->mini_buffer_completion_cache;
        if (client->_message.tag > Message::SHOW) {
            if (completion_cache->state == Completion_Cache::LOADED) {
                results_height = completion_cache->filter_context.results.len();
                if (results_height > editor->theme.max_completion_results) {
                    results_height = editor->theme.max_completion_results;
                }
                if (results_height > total_rows / 2) {
                    results_height = total_rows / 2;
                }
            }
        }

        size_t y = 0;
        size_t x = 0;
        size_t start_row = total_rows - mini_buffer_height - results_height;
        size_t start_col = 0;

        Face minibuffer_prompt_face = {};
        apply_face(&minibuffer_prompt_face,
                   editor->theme.special_faces[Face_Type::MINI_BUFFER_PROMPT]);
        {
            WITH_CONST_BUFFER(client->messages_id);
            Contents_Iterator it = buffer->contents.iterator_at(client->_message.start);
            for (; it.position < client->_message.end && y < 1; it.advance()) {
                SET_IND(minibuffer_prompt_face, it.get());
                ++x;
            }
        }

        if (client->_message.tag > Message::SHOW) {
            start_col = x;
            Window_Unified* window = client->mini_buffer_window();
            WITH_CONST_WINDOW_BUFFER(window);
            window->rows = mini_buffer_height;
            window->cols = total_cols - start_col;
            Contents_Iterator iterator =
                update_cursors_and_run_animation(editor, client, window, handle, buffer, nullptr);
            size_t cursor_pos_y, cursor_pos_x;
            draw_buffer_contents(cells, nullptr, total_cols, editor, client, buffer, window,
                                 start_row, start_col, {}, &cursor_pos_y, &cursor_pos_x, iterator);
        } else {
            for (; x < total_cols; ++x) {
                SET_IND({}, ' ');
            }

            if (std::chrono::system_clock::now() - client->_message_time >
                std::chrono::seconds(5)) {
                client->_message.tag = Message::NONE;
            }
        }

        {
            size_t y = 0;
            size_t x = 0;
            size_t start_row = total_rows - results_height;
            size_t start_col = 0;
            size_t offset = completion_cache->filter_context.selected;
            if (offset >= completion_cache->filter_context.results.len() - results_height / 2) {
                offset = completion_cache->filter_context.results.len() - results_height;
            } else if (offset < results_height / 2) {
                offset = 0;
            } else {
                offset -= results_height / 2;
            }
            for (size_t r = offset; r < results_height + offset; ++r) {
                {
                    Face face = {};
                    if (r == completion_cache->filter_context.selected) {
                        apply_face(&face,
                                   editor->theme
                                       .special_faces[Face_Type::MINI_BUFFER_COMPLETION_SELECTED]);
                    }

                    cz::Str result = completion_cache->filter_context.results[r];
                    for (size_t i = 0; i < total_cols && i < result.len; ++i) {
                        SET_IND(face, result[i]);
                        ++x;
                    }
                }

                for (; x < total_cols; ++x) {
                    SET_IND({}, ' ');
                }

                x = 0;
                ++y;
            }
        }

        total_rows = start_row;
    }

    draw_window(cells, window_cache, total_cols, editor, client, client->window,
                client->selected_normal_window, 0, 0, total_rows, total_cols, spqs);
}

}
}
