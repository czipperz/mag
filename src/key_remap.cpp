#include "key_remap.hpp"

#include <cz/heap.hpp>
#include <cz/str.hpp>

namespace mag {

void Key_Remap::drop() {
    transformations.drop(cz::heap_allocator());
}

static bool lookup_index(cz::Slice<const Key_Remap::Key_Transform> transformations,
                         Key key,
                         size_t* index) {
    size_t start = 0;
    size_t end = transformations.len;
    while (start < end) {
        size_t mid = (start + end) / 2;
        if (transformations[mid].in == key) {
            *index = mid;
            return true;
        } else if (transformations[mid].in < key) {
            start = mid + 1;
        } else {
            end = mid;
        }
    }

    *index = start;
    return false;
}

void Key_Remap::bind(cz::Str din, cz::Str dout) {
    Key in, out;
    if (!Key::parse(&in, din)) {
        CZ_PANIC("Key_Remap::bind(): Couldn't parse key description");
    }
    if (!Key::parse(&out, dout)) {
        CZ_PANIC("Key_Remap::bind(): Couldn't parse key description");
    }

    // Bind the transformation.
    size_t index;
    bool present = lookup_index(transformations, in, &index);
    if (present) {
        CZ_PANIC("Key_Remap::bind(): Mapping is already bound");
    } else {
        transformations.reserve(cz::heap_allocator(), 1);
        transformations.insert(index, {in, out});
    }
}

bool Key_Remap::bound(Key key) const {
    size_t index;
    bool present = lookup_index(transformations, key, &index);
    return present;
}

Key Key_Remap::get(Key key) const {
    size_t index;
    bool present = lookup_index(transformations, key, &index);
    CZ_DEBUG_ASSERT(present);
    return transformations[index].out;
}

}
