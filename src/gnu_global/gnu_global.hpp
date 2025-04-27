#pragma once

#include "generic.hpp"

namespace mag {
namespace gnu_global {

void init_completion_engine_context(Completion_Engine_Context* engine_context, char* directory);
bool completion_engine(Editor* editor, Completion_Engine_Context* context, bool is_initial_frame);

const char* lookup_symbol(const char* directory,
                          cz::Str query,
                          cz::Allocator allocator,
                          cz::Vector<tags::Tag>* tags);

}
}
