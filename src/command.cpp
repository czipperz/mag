#include "command.hpp"

#include <cz/sort.hpp>
#include <cz/str.hpp>

namespace mag {

cz::Heap_Vector<Command> global_commands;

void register_global_command(Command command) {
    global_commands.reserve(1);
    global_commands.push(command);
}

void sort_global_commands() {
    cz::sort(global_commands, [&](const Command* left, const Command* right) {
        return cz::Str{left->string} < cz::Str{right->string};
    });
}

}
