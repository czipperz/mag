#include "mouse_commands.hpp"

#include "command_macros.hpp"
#include "copy_commands.hpp"

namespace mag {
namespace basic {

static Job_Tick_Result mouse_motion_job_tick(Editor* editor, Client* client, void*) {
    if (!client->mouse.pressed_buttons[0]) {
        client->mouse.selecting = false;
        return Job_Tick_Result::FINISHED;
    }

    // Moved the mouse out of bounds.
    if (!client->mouse.window) {
        return Job_Tick_Result::STALLED;
    }

    WITH_CONST_SELECTED_NORMAL_BUFFER(client);
    Contents_Iterator iterator =
        nearest_character(window, buffer, client->mouse.window_row, client->mouse.window_column);

    kill_extra_cursors(window, client);

    window->cursors[0].point = iterator.position;
    if (iterator.position != window->cursors[0].mark) {
        window->show_marks = 2;
    } else {
        window->show_marks = false;
    }

#if 0
                Contents_Iterator* start;
                Contents_Iterator* end;
                if (mark.position < point.position) {
                    start = &mark;
                    end = &point;
                } else {
                    start = &point;
                    end = &mark;
                }

                if (client->mouse.mouse_down_data == MOUSE_SELECT_WORD) {
                    forward_char(start);
                    backward_word(start);
                    forward_word(end);
                } else {
                    start_of_line(start);
                    end_of_line(end);
                    forward_char(end);
                }

                spq.sp.window->show_marks = 2;
                spq.sp.window->cursors[0].mark = mark.position;
                spq.sp.window->cursors[0].point = point.position;
#endif

    // We are basically I/O bound so don't spin on the CPU.
    return Job_Tick_Result::STALLED;
}

static void mouse_motion_job_kill(void*) {}

void command_mouse_select_start(Editor* editor, Command_Source source) {
    if (!source.client->mouse.window || source.client->mouse.window->tag != Window::UNIFIED) {
        return;
    }

    source.client->selected_normal_window = (Window_Unified*)source.client->mouse.window;

    WITH_CONST_SELECTED_NORMAL_BUFFER(source.client);
    Contents_Iterator iterator = nearest_character(window, buffer, source.client->mouse.window_row,
                                                   source.client->mouse.window_column);
    kill_extra_cursors(window, source.client);
    window->cursors[0].point = window->cursors[0].mark = iterator.position;

    source.client->mouse.selecting = true;
    source.client->mouse.window_select_start_row = source.client->mouse.window_row;
    source.client->mouse.window_select_start_column = source.client->mouse.window_column;

    Synchronous_Job job;
    job.tick = mouse_motion_job_tick;
    job.kill = mouse_motion_job_kill;
    job.data = nullptr;
    editor->add_synchronous_job(job);
}

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
