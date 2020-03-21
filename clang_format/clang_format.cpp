#include "clang_format.hpp"

#include <stdio.h>
#include "command.hpp"
#include "command_macros.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "process.hpp"

namespace clang_format {

using namespace mag;

void command_clang_format_buffer(Editor* editor, Command_Source source) {
    WITH_SELECTED_BUFFER({
        char temp_file_buffer[L_tmpnam];
        tmpnam(temp_file_buffer);
        save_contents(&buffer->contents, temp_file_buffer);
        cz::Str temp_file_str = temp_file_buffer;

        cz::String script = {};
        CZ_DEFER(script.drop(cz::heap_allocator()));
        cz::Str base = "clang-format --output-replacements-xml ";
        script.reserve(cz::heap_allocator(), base.len + temp_file_str.len);
        script.append(base);
        script.append(temp_file_str);

        cz::String output_xml = {};
        CZ_DEFER(output_xml.drop(cz::heap_allocator()));
        int return_value;
        if (!run_process_synchronously("clang-format ", cz::heap_allocator(), &output_xml,
                                       &return_value) ||
            return_value != 0) {
            Message message = {};
            message.tag = Message::SHOW;
            message.text = "Error: Couldn't launch clang-format";
            source.client->show_message(message);
        }

        WITH_TRANSACTION({
            transaction.init(1, output_xml.len());

            Edit edit;
            edit.value.init_duplicate(transaction.value_allocator(), output_xml);
            edit.position = 0;
            edit.is_insert = true;
            transaction.push(edit);
        });
    });
}

}
