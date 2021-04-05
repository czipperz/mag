#include "theme.hpp"

#include "decoration.hpp"
#include "overlay.hpp"

namespace mag {

void Theme::drop(cz::Allocator allocator) {
    for (size_t i = 0; i < decorations.len(); ++i) {
        decorations[i].cleanup();
    }
    decorations.drop(allocator);

    for (size_t i = 0; i < overlays.len(); ++i) {
        overlays[i].cleanup();
    }
    overlays.drop(allocator);
}

}
