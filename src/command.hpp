#pragma once

#include <cz/heap_vector.hpp>
#include <cz/slice.hpp>

namespace mag {

struct Client;
struct Editor;
struct Key;
struct Command_Source;

///////////////////////////////////////////////////////////////////////////////
// Command definition
///////////////////////////////////////////////////////////////////////////////

using Command_Function = void (*)(Editor*, Command_Source);

struct Command {
    Command_Function function;
    const char* string;
};

#define COMMAND(FUNC) (Command{FUNC, sans_namespaces(#FUNC)})

struct Command_Source {
    Client* client;
    cz::Slice<const Key> keys;
    Command previous_command;
};

const char* sans_namespaces(const char* func);

///////////////////////////////////////////////////////////////////////////////
// Command registration
///////////////////////////////////////////////////////////////////////////////

extern cz::Heap_Vector<Command> global_commands;
void register_global_command(Command command);
void sort_global_commands();

struct Command_Registrar {
    Command_Registrar(Command command) { register_global_command(command); }
};

}
