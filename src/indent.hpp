#pragma once

namespace mag {

namespace Discover_Indent_Policy_ {
enum Discover_Indent_Policy {
    /// Go up a pair level then:
    /// * If we don't find an open pair discover 0 indent.
    /// * If the pair is at the end of the line go backwards till we reach
    ///   the start of the line then copy the indent and add one level.
    /// * Otherwise, indent at the same level as the pair.
    UP_THEN_BACK_PAIR,

    /// If the current line has indent then respect it, otherwise
    /// copy the indent of the first non-empty line before us.
    COPY_PREVIOUS_LINE,
};
}
using Discover_Indent_Policy_::Discover_Indent_Policy;

}
