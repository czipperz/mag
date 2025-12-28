#include "clang_format/clang_tidy.hpp"
#include <cz/format.hpp>
#include <cz/heap_string.hpp>
#include <cz/heap_vector.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/decoration.hpp"
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

void reset_buffer(const cz::Arc<Buffer_Handle> handle, cz::Str script) {
    WITH_BUFFER_HANDLE(handle);
    buffer->contents.remove(0, buffer->contents.len);
    buffer->contents.append(script);
    buffer->contents.append("\n");
}

void run_clang_tidy(Editor* editor,
                    Client* client,
                    const Buffer* buffer,
                    Buffer_State* state,
                    bool force_create_buffer) {
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

    cz::Arc<Buffer_Handle> clang_tidy_buffer_handle;
    if (force_create_buffer || !state->clang_tidy_buffer.upgrade(&clang_tidy_buffer_handle)) {
        cz::Heap_String buffer_name = cz::format("clang-tidy ", buffer->directory, buffer->name);
        CZ_DEFER(buffer_name.drop());
        if (!find_temp_buffer(editor, client, buffer_name, cz::Str{vc_root},
                              &clang_tidy_buffer_handle)) {
            clang_tidy_buffer_handle =
                editor->create_buffer(create_temp_buffer(buffer_name, cz::Str{vc_root}));
        }

        if (!force_create_buffer) {
            state->clang_tidy_buffer.drop();
        }
        state->clang_tidy_buffer = clang_tidy_buffer_handle.clone_downgrade();
    }

    reset_buffer(clang_tidy_buffer_handle, script);

    // TODO track if clang-tidy is installed / not setup correctly and stop running it.
    run_console_command_in(client, editor, clang_tidy_buffer_handle, vc_root.buffer, script);
}

void rerun_clang_tidy(Editor* editor,
                      Client* client,
                      const cz::Arc<Buffer_Handle>& handle,
                      const Buffer* buffer) {
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
        WITH_CONST_WINDOW_BUFFER((Window_Unified*)w, client);
        if (buffer->type != Buffer::FILE || !custom::should_run_clang_tidy(buffer)) {
            return;
        }
        rerun_clang_tidy(editor, client, handle, buffer);
    } else {
        Window_Split* window = (Window_Split*)w;
        run_clang_tidy_forall_changed_buffers_in_window(editor, client, window->first);
        run_clang_tidy_forall_changed_buffers_in_window(editor, client, window->second);
    }
}

void cleanup(Editor* editor) {
    for (size_t i = buffer_states.len; i-- > 0;) {
        if (!buffer_states[i].code_buffer.still_alive() ||
            (buffer_states[i].clang_tidy_buffer.is_null() ||
             !buffer_states[i].clang_tidy_buffer.still_alive())) {
            buffer_states[i].code_buffer.drop();
            if (buffer_states[i].clang_tidy_buffer.is_not_null()) {
                cz::Arc<Buffer_Handle> clang_tidy_buffer_handle;
                if (buffer_states[i].clang_tidy_buffer.upgrade(&clang_tidy_buffer_handle)) {
                    CZ_DEFER(clang_tidy_buffer_handle.drop());
                    editor->kill(clang_tidy_buffer_handle.get());
                }
                buffer_states[i].clang_tidy_buffer.drop();
            }
            buffer_states.remove(i);
        }
    }
}
}

void run_clang_tidy_forall_changed_buffers(Editor* editor, Client* client) {
    ZoneScoped;
    cleanup(editor);
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

REGISTER_COMMAND(command_alternate_clang_tidy);
void command_alternate_clang_tidy(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    if (buffer->type == Buffer::FILE && custom::should_run_clang_tidy(buffer)) {
        rerun_clang_tidy(editor, source.client, handle, buffer);

        for (size_t i = 0; i < buffer_states.len; ++i) {
            if (handle.ptr_equal(buffer_states[i].code_buffer)) {
                cz::Arc<Buffer_Handle> clang_tidy_buffer_handle;
                if (buffer_states[i].clang_tidy_buffer.upgrade(&clang_tidy_buffer_handle)) {
                    CZ_DEFER(clang_tidy_buffer_handle.drop());
                    source.client->set_selected_buffer(clang_tidy_buffer_handle);
                }
                break;
            }
        }
    } else if (buffer->type == Buffer::TEMPORARY && buffer->name.starts_with("*clang-tidy ") &&
               buffer->name.ends_with('*')) {
        open_file(editor, source.client,
                  buffer->name.slice(strlen("*clang-tidy "), buffer->name.len - 1));
    } else {
        source.client->show_message("Unsupported file type");
    }
}

static bool decoration_clang_tidy_append(Editor* editor,
                                         Client* client,
                                         const Buffer* buffer,
                                         Window_Unified* window,
                                         cz::Allocator allocator,
                                         cz::String* string,
                                         void* _data) {
    for (size_t i = 0; i < buffer_states.len; ++i) {
        cz::Arc<Buffer_Handle> handle = Buffer_Handle::cast_to_arc_handle_no_inc(buffer);
        if (!handle.ptr_equal(buffer_states[i].code_buffer) &&
            !handle.ptr_equal(buffer_states[i].clang_tidy_buffer)) {
            continue;
        }

        if (buffer_states[i].running) {
            cz::append(allocator, string, "clang-tidy...");
        }
        return true;
    }
    return false;
}
static void decoration_clang_tidy_cleanup(void* _data) {}

Decoration decoration_clang_tidy() {
    static const Decoration::VTable vtable = {decoration_clang_tidy_append,
                                              decoration_clang_tidy_cleanup};
    return {&vtable, nullptr};
}

}
