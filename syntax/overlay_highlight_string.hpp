#pragma once

#include "case.hpp"
#include "token.hpp"

namespace cz {
struct Str;
}

namespace mag {
struct Face;
struct Overlay;

namespace syntax {

/// Highlight every instance of `str` with the given `face`.
///
/// If `token_type` specified then results will only be
/// highlighted that start in a token with a matching type.
Overlay overlay_highlight_string(Face face,
                                 cz::Str str,
                                 Case_Handling case_handling = Case_Handling::CASE_SENSITIVE,
                                 Token_Type token_type = Token_Type::length);

}
}
