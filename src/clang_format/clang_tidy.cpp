#include "clang_format/clang_tidy.hpp"
#include <cz/format.hpp>
#include <cz/heap_string.hpp>
#include <cz/heap_vector.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"
#include "core/file.hpp"
#include "core/window.hpp"
#include "custom/config.hpp"
#include "version_control/version_control.hpp"

namespace mag {

namespace {
struct Buffer_State {
    cz::Arc_Weak<Buffer_Handle> code_buffer;
    cz::Option<Commit_Id> last_saved_commit_id;
    cz::Arc_Weak<Buffer_Handle> clang_tidy_buffer;
    bool running;
};

cz::Heap_Vector<Buffer_State> buffer_states;

void run_clang_tidy(Editor* editor,
                    Client* client,
                    const Buffer* buffer,
                    Buffer_State* state,
                    bool force_create_buffer) {
    cz::Arc<Buffer_Handle> clang_tidy_buffer_handle;
    if (force_create_buffer || !state->clang_tidy_buffer.upgrade(&clang_tidy_buffer_handle)) {
        cz::Heap_String buffer_name =
            cz::format("clang-tidy ", buffer->directory, buffer->name);
        CZ_DEFER(buffer_name.drop());
        if (!find_temp_buffer(editor, client, buffer_name, cz::Str{buffer->directory},
                              &clang_tidy_buffer_handle)) {
            clang_tidy_buffer_handle =
                editor->create_buffer(create_temp_buffer(buffer_name, cz::Str{buffer->directory}));
        }

        if (!force_create_buffer) {
            state->clang_tidy_buffer.drop();
        }
        state->clang_tidy_buffer = clang_tidy_buffer_handle.clone_downgrade();
    }

    state->running = true;  // Note don't retry if setting up the command fails.
    state->last_saved_commit_id = buffer->saved_commit_id;

    cz::Heap_String vc_root = {};
    CZ_DEFER(vc_root.drop());
    if (!version_control::get_root_directory(buffer->directory, cz::heap_allocator(), &vc_root))
        return;

    cz::Heap_String script = {};
    CZ_DEFER(script.drop());
    if (!custom::clang_tidy_script(vc_root, buffer, &script))
        return;

    // TODO track if clang-tidy is installed / not setup correctly and stop running it.
    run_console_command_in(client, editor, clang_tidy_buffer_handle, vc_root.buffer, script);
}

void run_clang_tidy_forall_changed_buffers_in_window_unified(Editor* editor,
                                                             Client* client,
                                                             Window_Unified* window) {
    WITH_CONST_WINDOW_BUFFER(window, client);

    // TODO make into custom predicate
    if (buffer->type != Buffer::FILE || !buffer->name.ends_with(".cpp")) {
        return;
    }

    for (size_t i = 0; i < buffer_states.len; ++i) {
        if (handle.ptr_equal(buffer_states[i].code_buffer)) {
            if (buffer->saved_commit_id != buffer_states[i].last_saved_commit_id &&
                !buffer_states[i].running) {
                run_clang_tidy(editor, client, buffer, &buffer_states[i], false);
            }
            return;
        }
    }

    buffer_states.reserve(1);
    buffer_states.push({handle.clone_downgrade()});
    run_clang_tidy(editor, client, buffer, &buffer_states.last(), true);
}

void run_clang_tidy_forall_changed_buffers_in_window(Editor* editor, Client* client, Window* w) {
    if (w->tag == Window::UNIFIED) {
        return run_clang_tidy_forall_changed_buffers_in_window_unified(editor, client,
                                                                       (Window_Unified*)w);
    } else {
        Window_Split* window = (Window_Split*)w;
        run_clang_tidy_forall_changed_buffers_in_window(editor, client, window->first);
        run_clang_tidy_forall_changed_buffers_in_window(editor, client, window->second);
    }
}

void cleanup() {
    for (size_t i = buffer_states.len; i-- > 0;) {
        if (!buffer_states[i].code_buffer.still_alive() ||
            !buffer_states[i].clang_tidy_buffer.still_alive()) {
            buffer_states[i].code_buffer.drop();
            buffer_states[i].clang_tidy_buffer.drop();
            buffer_states.remove(i);
        }
    }
}
}

void run_clang_tidy_forall_changed_buffers(Editor* editor, Client* client) {
    ZoneScoped;
    cleanup();
    run_clang_tidy_forall_changed_buffers_in_window(editor, client, client->window);
}

void mark_clang_tidy_done(const cz::Arc<Buffer_Handle>& buffer_handle) {
    for (size_t i = 0; i < buffer_states.len; ++i) {
        if (buffer_states[i].clang_tidy_buffer.ptr_equal(buffer_handle)) {
            buffer_states[i].running = false;
            break;
        }
    }
}

}
