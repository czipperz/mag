#pragma once

#include <cz/heap_string.hpp>
#include <cz/slice.hpp>

namespace mag {
struct Client;

/// Run a process.  Fails on I/O error, or if stderr is produced, or if the
/// process produces an error.  On error, shows a message to the client and
/// returns `false`.  On success, writes stdout to `out` and returns `true`.
bool run_process_for_output(Client* client,
                            cz::Slice<cz::Str> args,
                            cz::Str pretty_name,
                            const char* working_directory,
                            cz::Heap_String* out);

/// Run a process.  Fails on I/O error, returns 128, and writes
/// an error message to `err`.  Otherwise returns the exit code.
int run_process_for_output(cz::Slice<cz::Str> args,
                           cz::Str pretty_name,
                           const char* working_directory,
                           cz::Heap_String* out,
                           cz::Heap_String* err);

}
