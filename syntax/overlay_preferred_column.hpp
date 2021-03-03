#pragma once

#include <stdint.h>

namespace mag {
struct Face;
struct Overlay;

namespace syntax {

Overlay overlay_preferred_column(Face face, uint64_t tab_width, uint64_t column);

}
}
