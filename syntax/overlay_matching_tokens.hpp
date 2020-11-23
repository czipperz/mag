#pragma once

#include <cz/slice.hpp>
#include "token.hpp"

namespace mag {
struct Face;
struct Overlay;

namespace syntax {

Overlay overlay_matching_tokens(Face face, cz::Slice<const Token_Type> token_types);

}
}
