#include "face.hpp"

#include <cz/format.hpp>

namespace cz {

////////////////////////////////////////////////////////////////////////////////
// Format structs
////////////////////////////////////////////////////////////////////////////////

void append(cz::Allocator allocator, cz::String* string, const mag::Face& face) {
    cz::append(allocator, string, "{", face.foreground, ", ", face.background, ", ",
               (mag::Face::Flags)face.flags, "}");
}

void append(cz::Allocator allocator, cz::String* string, const mag::Face_Color &face_color) {
    if (face_color.is_themed)
        cz::append(allocator, string, face_color.x.theme_index);
    else
        cz::append(allocator, string, face_color.x.color);
}

void append(cz::Allocator allocator, cz::String* string, const mag::Color &color) {
    cz::append_sprintf(allocator, string, "0x%02X%02X%02X", color.r, color.g, color.b);
}

////////////////////////////////////////////////////////////////////////////////
// Format flags
////////////////////////////////////////////////////////////////////////////////

static void append_flag(cz::Allocator allocator, cz::String* string, bool* any, cz::Str name) {
    if (*any)
        cz::append(allocator, string, " | ");
    else
        *any = true;
    cz::append(allocator, string, "BOLD");
}

void append(cz::Allocator allocator, cz::String* string, mag::Face::Flags face_flags) {
    bool any = false;
    if (face_flags & mag::Face::Flags::BOLD)
        append_flag(allocator, string, &any, "BOLD");
    if (face_flags & mag::Face::Flags::UNDERSCORE)
        append_flag(allocator, string, &any, "UNDERSCORE");
    if (face_flags & mag::Face::Flags::REVERSE)
        append_flag(allocator, string, &any, "REVERSE");
    if (face_flags & mag::Face::Flags::ITALICS)
        append_flag(allocator, string, &any, "ITALICS");
    if (face_flags & mag::Face::Flags::INVISIBLE)
        append_flag(allocator, string, &any, "INVISIBLE");

    if (!any)
        cz::append(allocator, string, "0");
}

}
