#include "command.hpp"

namespace mag {

cz::Heap_Vector<Command> global_commands;

void register_global_command(Command command) {
    global_commands.reserve(1);
    global_commands.push(command);
}

}
