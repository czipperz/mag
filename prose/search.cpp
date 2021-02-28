#include "alternate.hpp"

#include "command_macros.hpp"
#include "file.hpp"
#include "movement.hpp"
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

    run_console_command(client, editor, directory, {args, (size_t)(3 + query_word)}, buffer_name, "Ag error");
}

static char* copy_directory(const cz::String& buffer_directory) {
    char* directory = nullptr;
    if (buffer_directory.len() > 0) {
        directory = (char*)malloc(buffer_directory.len() + 1);
        CZ_ASSERT(directory);

        memcpy(directory, buffer_directory.buffer(), buffer_directory.len());
        directory[buffer_directory.len()] = '\0';
    }
    return directory;
}

static void command_search_in_current_directory_callback(Editor* editor,
                                                         Client* client,
                                                         cz::Str query,
                                                         void* data) {
    char* directory = nullptr;
    CZ_DEFER(free(directory));

    {
        WITH_BUFFER(*(Buffer_Id*)data);
        directory = copy_directory(buffer->directory);
    }

    run_ag(client, editor, directory, query, false);
}

void command_search_in_current_directory(Editor* editor, Command_Source source) {
    Buffer_Id* selected_buffer_id = (Buffer_Id*)malloc(sizeof(Buffer_Id));
    CZ_ASSERT(selected_buffer_id);
    *selected_buffer_id = source.client->selected_window()->id;
    source.client->show_dialog(editor, "ag: ", no_completion_engine,
                               command_search_in_current_directory_callback, selected_buffer_id);
}

void command_search_in_current_directory_token_at_position(Editor* editor, Command_Source source) {
    char* directory = nullptr;
    CZ_DEFER(free(directory));

    cz::String query = {};
    CZ_DEFER(query.drop(cz::heap_allocator()));

    {
        WITH_SELECTED_BUFFER(source.client);

        directory = copy_directory(buffer->directory);

        Contents_Iterator iterator = buffer->contents.iterator_at(window->cursors[0].point);
        Token token;
        if (!get_token_at_position(buffer, &iterator, &token)) {
            source.client->show_message("Cursor is not positioned at a token");
            return;
        }

        query.reserve(cz::heap_allocator(), token.end - token.start);
        buffer->contents.slice_into(iterator, token.end, &query);
    }

    run_ag(source.client, editor, directory, query, true);
}

}
}
