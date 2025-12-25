#include "token.hpp"

#include <cz/format.hpp>
#include "core/face.hpp"

namespace mag {
namespace Token_Type_ {

extern const char* const names[/*Token_Type::length*/] = {
#define X(name) #name,
    MAG_TOKEN_TYPES(X)
#undef X
};

Token_Type encode(Face face) {
    uint64_t type = CUSTOM;

    if (face.foreground.is_themed) {
        type |= ((uint64_t)(uint16_t)face.foreground.x.theme_index << 32);
    } else {
        type |= CUSTOM_FOREGROUND_IS_COLOR | ((uint64_t)face.foreground.x.color.r << 48) |
                ((uint64_t)face.foreground.x.color.g << 40) |
                ((uint64_t)face.foreground.x.color.b << 32);
    }

    if (face.background.is_themed) {
        type |= ((uint64_t)(uint16_t)face.background.x.theme_index << 0);
    } else {
        type |= CUSTOM_BACKGROUND_IS_COLOR | ((uint64_t)face.background.x.color.r << 16) |
                ((uint64_t)face.background.x.color.g << 8) |
                ((uint64_t)face.background.x.color.b << 0);
    }

    if (face.flags & Face::INVISIBLE) {
        type |= CUSTOM_FACE_INVISIBLE;
    }
    type |= ((face.flags & 0xF) << 24);

    return (Token_Type)type;
}

Face decode(Token_Type type) {
    Face face;

    if (type & CUSTOM_FOREGROUND_IS_COLOR) {
        face.foreground.is_themed = false;
        face.foreground.x.color.r = ((type >> 48) & 0xFF);
        face.foreground.x.color.g = ((type >> 40) & 0xFF);
        face.foreground.x.color.b = ((type >> 32) & 0xFF);
    } else {
        face.foreground.is_themed = true;
        face.foreground.x.theme_index = (int16_t)((type >> 32) & 0xFFFF);
    }

    if (type & CUSTOM_BACKGROUND_IS_COLOR) {
        face.background.is_themed = false;
        face.background.x.color.r = ((type >> 16) & 0xFF);
        face.background.x.color.g = ((type >> 8) & 0xFF);
        face.background.x.color.b = ((type >> 0) & 0xFF);
    } else {
        face.background.is_themed = true;
        face.background.x.theme_index = (int16_t)((type >> 0) & 0xFFFF);
    }

    face.flags = 0;
    if (type & CUSTOM_FACE_INVISIBLE) {
        face.flags |= Face::INVISIBLE;
    }
    face.flags |= ((type >> 24) & 0xF);

    return face;
}

}

bool Token::is_valid(uint64_t contents_len) const {
    if (start > end)
        return false;
    if (end > contents_len)
        return false;
    if (!(type & Token_Type::CUSTOM)) {
        if (type >= Token_Type::length)
            return false;
    }
    return true;
}

void Token::assert_valid(uint64_t contents_len) const {
    CZ_ASSERT(start <= end);
    CZ_ASSERT(end <= contents_len);
    if (!(type & Token_Type::CUSTOM))
        CZ_ASSERT(type < Token_Type::length);
}

bool Token::contains_position(uint64_t position) const {
    return position >= start && position < end;
}
}

namespace cz {
void append(cz::Allocator allocator, cz::String* string, const mag::Token& token) {
    cz::append(allocator, string, "{", token.start, ", ", token.end, ", ", token.type, "}");
}

void append(cz::Allocator allocator, cz::String* string, mag::Token_Type type) {
    if (type & mag::Token_Type::CUSTOM) {
        // The type is an encoded Face.  Decode it and format it as a Face.
        cz::append(allocator, string, mag::Token_Type_::decode(type));
    } else if (type < mag::Token_Type::length) {
        cz::append(allocator, string, "Token_Type::", mag::Token_Type_::names[type]);
    } else {
        cz::append(allocator, string, "<invalid Token_Type>");
    }
}
}
