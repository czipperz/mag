#include "diff_commands.hpp"

#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include "core/buffer.hpp"
#include "core/client.hpp"
#include "core/command_macros.hpp"
#include "core/diff.hpp"
#include "core/edit.hpp"
#include "core/file.hpp"
#include "core/transaction.hpp"
#include "syntax/tokenize_path.hpp"

namespace mag {
namespace basic {

static void command_apply_diff_callback(Editor* editor,
                                        Client* client,
                                        cz::Str diff_file,
                                        void* data) {
    cz::String path = diff_file.clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(path.drop(cz::heap_allocator()));

    cz::Input_File file;
    if (!file.open(path.buffer)) {
        client->show_message("Error opening diff file");
        return;
    }
    CZ_DEFER(file.close());

    WITH_SELECTED_BUFFER(client);
    const char* message = apply_diff_file(buffer, file);
    if (message)
        client->show_message(message);
}

REGISTER_COMMAND(command_apply_diff);
void command_apply_diff(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Diff to apply: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_apply_diff_callback;
    dialog.next_token = syntax::path_next_token;
    source.client->show_dialog(dialog);
}

}
}
