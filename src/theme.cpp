#include "theme.hpp"

#include "decoration.hpp"
#include "overlay.hpp"

namespace mag {

void Theme::drop() {
    for (size_t i = 0; i < decorations.len(); ++i) {
        decorations[i].cleanup();
    }
    decorations.drop();

    for (size_t i = 0; i < overlays.len(); ++i) {
        overlays[i].cleanup();
    }
    overlays.drop();
}

}
