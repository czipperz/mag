#include "key_map.hpp"

#include <cz/heap.hpp>

namespace mag {

static bool lookup_index(Key_Map* key_map, Key key, size_t* out) {
    size_t start = 0;
    size_t end = key_map->bindings.len();
    while (start < end) {
        size_t mid = (start + end) / 2;
        if (key_map->bindings[mid].key == key) {
            *out = mid;
            return true;
        } else if (key_map->bindings[mid].key < key) {
            start = mid + 1;
        } else {
            end = mid;
        }
    }

    *out = start;
    return false;
}

Key_Bind* Key_Map::lookup(Key key) {
    size_t i;
    if (lookup_index(this, key, &i)) {
        return &bindings[i];
    } else {
        return nullptr;
    }
}

static void parse_key(Key* key, size_t* i, cz::Str description) {
    if (*i + 1 < description.len && description[*i] == 'C' && description[*i + 1] == '-') {
        key->modifiers |= CONTROL;
        *i += 2;
    }
    if (*i + 1 < description.len && description[*i] == 'A' && description[*i + 1] == '-') {
        key->modifiers |= ALT;
        *i += 2;
    }
    if (*i + 1 < description.len && description[*i] == 'S' && description[*i + 1] == '-') {
        key->modifiers |= SHIFT;
        *i += 2;
    }

    CZ_ASSERT(*i < description.len);
    if (*i + 1 < description.len && description[*i] == '\\') {
        char escaped_char = description[*i + 1];
        CZ_ASSERT(escaped_char == ' ' || escaped_char == '\\' || escaped_char == '-');
        if (escaped_char == '-') {
            key->code = 127;
        } else {
            key->code = escaped_char;
        }
        *i += 2;
    } else {
        key->code = description[*i];
        *i += 1;
    }
}

void Key_Map::bind(cz::Str description, Command command) {
    size_t i = 0;
    Key_Map* key_map = this;

    while (1) {
        Key key = {};
        parse_key(&key, &i, description);

        size_t bind_index;
        if (lookup_index(key_map, key, &bind_index)) {
            Key_Bind* bind = &key_map->bindings[bind_index];
            if (bind->is_command) {
                CZ_ASSERT(i == description.len);
                bind->v.command = command;
                return;
            } else {
                key_map = bind->v.map;
            }
        } else {
            Key_Bind bind;
            bind.key = key;
            key_map->bindings.reserve(cz::heap_allocator(), 1);
            if (i == description.len) {
                bind.is_command = true;
                bind.v.command = command;
                key_map->bindings.insert(bind_index, bind);
                return;
            } else {
                bind.is_command = false;
                Key_Map* new_map = cz::heap_allocator().alloc<Key_Map>();
                *new_map = {};
                bind.v.map = new_map;
                key_map->bindings.insert(bind_index, bind);
                key_map = new_map;
            }
        }

        CZ_ASSERT(i < description.len);
        CZ_ASSERT(description[i] == ' ');
        ++i;
    }
}

void Key_Map::drop() {
    for (size_t b = 0; b < bindings.len(); ++b) {
        if (!bindings[b].is_command) {
            bindings[b].v.map->drop();
            cz::heap_allocator().dealloc({bindings[b].v.map, sizeof(Key_Map)});
        }
    }
    bindings.drop(cz::heap_allocator());
}

}
