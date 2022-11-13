#pragma once

#include "generic.hpp"

namespace mag {
namespace ctags {

const char* list_symbols(cz::Str directory, cz::Allocator allocator, cz::Vector<cz::Str>* symbols);

void init_completion_engine_context(Completion_Engine_Context* engine_context, char* directory);
bool completion_engine(Editor* editor, Completion_Engine_Context* context, bool is_initial_frame);

const char* lookup_symbol(cz::Str directory,
                          cz::Str symbol,
                          cz::Allocator allocator,
                          cz::Vector<tags::Tag>* tags);

}
}
