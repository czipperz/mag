#pragma once

namespace cz {
struct Str;
}

namespace mag {
struct Face;
struct Overlay;

namespace syntax {

Overlay overlay_highlight_string(Face face, cz::Str str, bool case_insensitive);

}
}
