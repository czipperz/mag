#include "mouse_commands.hpp"

#include "command_macros.hpp"
#include "copy_commands.hpp"
#include "movement.hpp"

namespace mag {
namespace basic {

static int word_char_category(char ch) {
    if (cz::is_alnum(ch))
        return 1;
    else if (cz::is_space(ch))
        return 2;
    else
        return 3;
}

static Job_Tick_Result mouse_motion_job_tick(Editor* editor, Client* client, void*) {
    // Moved the mouse out of bounds.
    if (!client->mouse.window) {
        return Job_Tick_Result::STALLED;
    }

    WITH_CONST_SELECTED_NORMAL_BUFFER(client);
    kill_extra_cursors(window, client);

    if (client->mouse.window_select_point > buffer->contents.len) {
        client->mouse.window_select_point = buffer->contents.len;
    }

    Contents_Iterator mark = buffer->contents.iterator_at(client->mouse.window_select_point);
    Contents_Iterator point = nearest_character(
        window, buffer, editor->theme, client->mouse.window_row, client->mouse.window_column);

    // Word and line selection.
    if (client->mouse.selecting > 1) {
        Contents_Iterator* start;
        Contents_Iterator* end;
        if (mark.position < point.position) {
            start = &mark;
            end = &point;
        } else {
            start = &point;
            end = &mark;
        }

        if (client->mouse.selecting == 2) {
            // Expand backwards while in the same category or until we hit a newline.
            forward_char(start);
            int cat = -1;
            while (!start->at_bob()) {
                start->retreat();
                char ch = start->get();
                if (ch == '\n') {
                    start->advance();
                    break;
                }
                int ncat = word_char_category(ch);
                if (cat == -1)
                    cat = ncat;
                if (ncat != cat) {
                    start->advance();
                    break;
                }
            }

            // Expand forwards while in the same category or until we hit a newline.
            cat = -1;
            while (!end->at_eob()) {
                char ch = end->get();
                if (ch == '\n') {
                    break;
                }
                int ncat = word_char_category(ch);
                if (cat == -1)
                    cat = ncat;
                if (ncat != cat) {
                    break;
                }
                end->advance();
            }
        } else {
            start_of_line(start);
            end_of_line(end);
            forward_char(end);
        }
    }

    if (mark.position != point.position) {
        window->show_marks = 2;
    } else {
        window->show_marks = false;
    }
    window->cursors[0].mark = mark.position;
    window->cursors[0].point = point.position;

    // If the mouse has been released then stop.  We process this frame even
    // though the mouse has been released because it allows for ncurses (which
    // only updates the mouse position on a press/release event) to select text.
    if (!client->mouse.pressed_buttons[0]) {
        client->mouse.selecting = false;
        return Job_Tick_Result::FINISHED;
    }

    // We are basically I/O bound so don't spin on the CPU.
    return Job_Tick_Result::STALLED;
}

static void mouse_motion_job_kill(void*) {}

static uint32_t mouse_click_length;
static uint32_t mouse_click_row, mouse_click_column;
static std::chrono::high_resolution_clock::time_point mouse_click_time;
static const std::chrono::milliseconds mouse_click_elapsed{500};

static void start_selecting(Editor* editor, Client* client, bool set_selection_start) {
    if (!client->mouse.window || client->mouse.window->tag != Window::UNIFIED) {
        return;
    }

    client->selected_normal_window = (Window_Unified*)client->mouse.window;

    WITH_CONST_SELECTED_NORMAL_BUFFER(client);
    Contents_Iterator iterator = nearest_character(
        window, buffer, editor->theme, client->mouse.window_row, client->mouse.window_column);
    kill_extra_cursors(window, client);
    window->cursors[0].point = window->cursors[0].mark = iterator.position;

    // Reset click chain if too much time has elapsed or mouse moved.
    auto now = std::chrono::high_resolution_clock::now();
    if (now - mouse_click_time > mouse_click_elapsed ||
        mouse_click_row != client->mouse.window_row ||
        mouse_click_column != client->mouse.window_column) {
        mouse_click_length = 0;
    }

    // Record click.
    ++mouse_click_length;
    mouse_click_time = now;
    client->mouse.selecting = (mouse_click_length - 1) % 3 + 1;

    // Set the selection start position.
    if (set_selection_start) {
        mouse_click_row = client->mouse.window_row;
        mouse_click_column = client->mouse.window_column;
        client->mouse.window_select_point = iterator.position;
    }

    Synchronous_Job job;
    job.tick = mouse_motion_job_tick;
    job.kill = mouse_motion_job_kill;
    job.data = nullptr;
    editor->add_synchronous_job(job);
}

REGISTER_COMMAND(command_mouse_select_start);
void command_mouse_select_start(Editor* editor, Command_Source source) {
    start_selecting(editor, source.client, true);
}

REGISTER_COMMAND(command_mouse_select_continue);
void command_mouse_select_continue(Editor* editor, Command_Source source) {
    start_selecting(editor, source.client, false);
}

REGISTER_COMMAND(command_copy_paste);
void command_copy_paste(Editor* editor, Command_Source source) {
    bool copy;
    {
        WITH_CONST_SELECTED_BUFFER(source.client);
        // Copy a region or paste if no region.
        copy = window->show_marks;
    }

    if (copy) {
        command_copy(editor, source);
    } else {
        command_paste(editor, source);
    }
}

}
}
