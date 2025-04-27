#pragma once

#include <stdint.h>

namespace mag {

namespace Discover_Indent_Policy_ {
enum Discover_Indent_Policy {
    /// Go backwards by pair until we reach the start of the line then
    /// copy the indent and add one level.  If we reach a pair in the
    /// middle of the line then indent at the same level as the pair.
    ///
    /// Unlike `UP_THEN_BACK_PAIR`, custom indent will be carried over onto future lines.
    /// This makes certain cases less common cases like labels that are indented less be
    /// pessimized while other much more common cases like namespaces are handled correctly.
    BACK_PAIR,

    /// Go up a pair level then:
    /// * If we don't find an open pair discover 0 indent.
    /// * If the pair is at the end of the line go backwards till we reach
    ///   the start of the line then copy the indent and add one level.
    /// * Otherwise, indent at the same level as the pair.
    ///
    /// Note: UP_THEN_BACK_PAIR only works if the language use
    /// pairs (`(,)`, `{,}`, `[,]`, `begin,end`) to denote scope.
    UP_THEN_BACK_PAIR,

    /// If the current line has indent then respect it, otherwise
    /// copy the indent of the first non-empty line before us.
    COPY_PREVIOUS_LINE,
};
}
using Discover_Indent_Policy_::Discover_Indent_Policy;

struct Buffer;
struct Contents_Iterator;

/// Find the indent width for an operation on the line of the iterator.
uint64_t find_indent_width(Buffer* buffer, Contents_Iterator it);
uint64_t find_indent_width(Buffer* buffer, Contents_Iterator it, Discover_Indent_Policy);

}
