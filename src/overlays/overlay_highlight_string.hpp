#pragma once

#include "core/case.hpp"
#include "core/token.hpp"

namespace cz {
struct Str;
}

namespace mag {
struct Face;
struct Overlay;

namespace syntax {

namespace Matching_Algo_ {
enum Matching_Algo {
    CONTAINS,
    EXACT_MATCH,
    PREFIX,
    SUFFIX,
};
}
using Matching_Algo_::Matching_Algo;

/// Highlight every instance of `str` with the given `face`.
///
/// If `token_type` specified then results will only be
/// highlighted if they are in a token with a matching type.
///
/// `matching_algo` allows additionally constraining `token_type` to only consider a specific
/// part of the token.  If `token_type = Token_Type::length` then `matching_algo` is ignored.
Overlay overlay_highlight_string(Face face,
                                 cz::Str str,
                                 Case_Handling case_handling = Case_Handling::CASE_SENSITIVE,
                                 Token_Type token_type = Token_Type::length,
                                 Matching_Algo matching_algo = Matching_Algo::CONTAINS);

bool is_overlay_highlight_string(const Overlay& overlay, cz::Str str);

}
}
