#pragma once

#include "core/overlay.hpp"
#include "prose/compiler.hpp"

namespace mag {
namespace syntax {

Overlay overlay_compiler_messages();

bool is_overlay_compiler_messages(const Overlay&);
void set_overlay_compiler_messages(Overlay*, prose::All_Messages);

}
}
