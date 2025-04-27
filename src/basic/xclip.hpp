#pragma once

#include <cz/string.hpp>

namespace mag {
struct Client;

namespace xclip {

/// Get the primary clipboard's contents.  Uses `xclip` by
/// default.  If `xclip` is not present then uses `xsel`.
bool get_clipboard(void*, cz::Allocator allocator, cz::String* text);

/// Set the primary clipboard's contents.  Only works with `xclip`.
bool set_clipboard(void*, cz::Str text);

/// Use `xclip` / `xsel` to access the system clipboard instead of the default.
/// Returns `false` if neither `xclip` nor `xsel` is in the path.
/// Note: `xsel` has read-only access to the clipboard whereas `xclip` has read/write.
bool use_xclip_clipboard(Client* client);

}
}
