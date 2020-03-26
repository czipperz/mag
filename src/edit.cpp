#include "edit.hpp"

namespace mag {

static void position_after_insert_after(uint64_t position, uint64_t len, uint64_t* point) {
    if (*point > position) {
        *point += len;
    }
}

static void position_after_insert_before(uint64_t position, uint64_t len, uint64_t* point) {
    if (*point >= position) {
        *point += len;
    }
}

static void position_after_remove(uint64_t position, uint64_t len, uint64_t* point) {
    if (*point >= position) {
        if (*point >= position + len) {
            *point -= len;
        } else {
            *point = position;
        }
    }
}

void position_after_edits(cz::Slice<const Edit> edits, uint64_t* position) {
    for (size_t i = 0; i < edits.len; ++i) {
        const Edit* edit = &edits[i];
        if (edit->flags & Edit::INSERT_MASK) {
            if (edit->flags & Edit::AFTER_POSITION_MASK) {
                position_after_insert_after(edit->position, edit->value.len(), position);
            } else {
                position_after_insert_before(edit->position, edit->value.len(), position);
            }
        } else {
            position_after_remove(edit->position, edit->value.len(), position);
        }
    }
}

void position_before_edits(cz::Slice<const Edit> edits, uint64_t* position) {
    for (size_t i = edits.len; i-- > 0;) {
        const Edit* edit = &edits[i];
        if (edit->flags & Edit::INSERT_MASK) {
            position_after_remove(edit->position, edit->value.len(), position);
        } else {
            if (edit->flags & Edit::AFTER_POSITION_MASK) {
                position_after_insert_after(edit->position, edit->value.len(), position);
            } else {
                position_after_insert_before(edit->position, edit->value.len(), position);
            }
        }
    }
}

}
