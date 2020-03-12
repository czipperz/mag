#include "directory_commands.hpp"

#include "command.hpp"
#include "editor.hpp"

#include "client.hpp"
#include "message.hpp"

namespace mag {

void command_directory_open_path(Editor* editor, Command_Source source) {
    Message message = {};
    message.tag = Message::SHOW;
    message.text = "File path must not be empty";
    source.client->show_message(message);
}

}
