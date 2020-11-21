#include "cpp_commands.hpp"

#include "command_macros.hpp"
#include "editor.hpp"
#include "movement.hpp"
#include "transaction.hpp"
#include "window.hpp"

namespace mag {
namespace cpp {

void command_comment(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER(source.client);
    cz::Slice<Cursor> cursors = window->cursors;

    Transaction transaction;
    if (window->show_marks) {
        return;
    } else {
        transaction.init(cursors.len, 0);

        SSOStr value;
        value = SSOStr::from_constant("// ");

        uint64_t offset = 0;
        for (size_t c = 0; c < cursors.len; ++c) {
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);
            start_of_line_text(&start);

            Edit edit;
            edit.value = value;
            edit.position = start.position + offset;
            offset += 3;
            edit.flags = Edit::INSERT;
            transaction.push(edit);
        }
    }

    transaction.commit(buffer);
}

}
}
