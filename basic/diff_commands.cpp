#include "diff_commands.hpp"

#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>
#include <cz/try.hpp>
#include "buffer.hpp"
#include "client.hpp"
#include "command_macros.hpp"
#include "diff.hpp"
#include "edit.hpp"
#include "file.hpp"
#include "transaction.hpp"

namespace mag {
namespace basic {

static void command_apply_diff_callback(Editor* editor,
                                        Client* client,
                                        cz::Str diff_file,
                                        void* data) {
    cz::String path = diff_file.clone_null_terminate(cz::heap_allocator());
    CZ_DEFER(path.drop(cz::heap_allocator()));

    cz::Input_File file;
    if (!file.open(path.buffer())) {
        client->show_message(editor, "Error opening diff file");
        return;
    }
    CZ_DEFER(file.close());

    WITH_SELECTED_BUFFER(client);
    apply_diff_file(editor, client, buffer, file);
}

void command_apply_diff(Editor* editor, Command_Source source) {
    Dialog dialog = {};
    dialog.prompt = "Diff to apply: ";
    dialog.completion_engine = file_completion_engine;
    dialog.response_callback = command_apply_diff_callback;
    source.client->show_dialog(editor, dialog);
}

}
}
