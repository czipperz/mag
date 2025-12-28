#pragma once

#include <cz/arc.hpp>

namespace mag {

struct Buffer_Handle;
struct Client;
struct Command_Source;
struct Decoration;
struct Editor;

void run_clang_tidy_forall_changed_buffers(Editor* editor, Client* client);
void mark_clang_tidy_done(const cz::Arc<Buffer_Handle>& buffer_handle);

void command_alternate_clang_tidy(Editor* editor, Command_Source source);

Decoration decoration_clang_tidy();

}
