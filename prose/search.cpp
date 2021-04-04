#include "alternate.hpp"

#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"
#include "overlay.hpp"
#include "syntax/overlay_highlight_string.hpp"
#include "token.hpp"

namespace mag {
namespace prose {

static void run_ag(Client* client,
                   Editor* editor,
                   const char* directory,
                   cz::Str query,
                   bool query_word) {
    cz::Str args[] = {"ag", "--column", query, "-w"};

    cz::String buffer_name = {};
    CZ_DEFER(buffer_name.drop(cz::heap_allocator()));
    buffer_name.reserve(cz::heap_allocator(), 3 + query.len);
    buffer_name.append("ag ");
    buffer_name.append(query);

    cz::Arc<Buffer_Handle> handle;
    if (run_console_command(client, editor, directory, {args, (size_t)(3 + query_word)},
                            buffer_name, "Ag error", &handle)) {
        Buffer* buffer = handle->lock_writing();
        CZ_DEFER(handle->unlock());
        buffer->mode.overlays.reserve(cz::heap_allocator(), 1);
        buffer->mode.overlays.push(syntax::overlay_highlight_string(editor->theme.faces[12], query,
                                                                    /*case_insensitive=*/false));
    }
}

static char* copy_directory(cz::Str buffer_directory) {
    if (buffer_directory.len > 0) {
        return buffer_directory.duplicate_null_terminate(cz::heap_allocator()).buffer();
    }
    return nullptr;
}

static void command_search_in_current_directory_callback(Editor* editor,
                                                         Client* client,
                                                         cz::Str query,
                                                         void* data) {
    char* directory = nullptr;
    CZ_DEFER(cz::heap_allocator().dealloc({directory, 1}));

    {
        WITH_CONST_BUFFER(*(Buffer_Id*)data);
        directory = copy_directory(buffer->directory);
    }

    run_ag(client, editor, directory, query, false);
}

void command_search_in_current_directory(Editor* editor, Command_Source source) {
    Buffer_Id* selected_buffer_id = cz::heap_allocator().alloc<Buffer_Id>();
    CZ_ASSERT(selected_buffer_id);
    *selected_buffer_id = source.client->selected_window()->id;
    source.client->show_dialog(editor, "ag: ", no_completion_engine,
                               command_search_in_current_directory_callback, selected_buffer_id);
    source.client->fill_mini_buffer_with_selected_region(editor);
}

void command_search_in_current_directory_token_at_position(Editor* editor, Command_Source source) {
    char* directory = nullptr;
    CZ_DEFER(cz::heap_allocator().dealloc({directory, 1}));

    cz::String query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);

        directory = copy_directory(buffer->directory);

        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message(editor, "Cursor is not positioned at a token");
            return;
        }

        query.reserve(cz::heap_allocator(), token.end - token.start);
        buffer->contents.slice_into(iterator, token.end, &query);
    }

    run_ag(source.client, editor, directory, query, true);
}

}
}
