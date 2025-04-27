#include "mode.hpp"

#include <cz/heap.hpp>
#include "core/decoration.hpp"
#include "core/overlay.hpp"

namespace mag {

void Mode::drop() {
    key_map.drop();
    completion_key_map.drop();

    for (size_t i = 0; i < decorations.len; ++i) {
        decorations[i].cleanup();
    }
    decorations.drop();

    for (size_t i = 0; i < overlays.len; ++i) {
        overlays[i].cleanup();
    }
    overlays.drop();
}

}
