#include "decoration_line_number.hpp"

#include <stdlib.h>
#include <Tracy.hpp>
#include <algorithm>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <cz/write.hpp>
#include "buffer.hpp"
#include "decoration.hpp"
#include "movement.hpp"
#include "window.hpp"

namespace mag {
namespace syntax {

struct Buffer_Data {
    Buffer_Id id;
    size_t change_index;
    uint64_t position;
    uint64_t line_number;
};

struct Data {
    cz::Vector<Buffer_Data> buffer_datas;
};

static void update_insert(const Edit& edit, Buffer_Data* data) {
    if (data->position > edit.position ||
        (!(edit.flags & Edit::AFTER_POSITION_MASK) && data->position == edit.position)) {
        data->position += edit.value.len();
        data->line_number += edit.value.as_str().count('\n');
    }
}

static void update_remove(const Edit& edit, Buffer_Data* data) {
    if (edit.position + edit.value.len() <= data->position) {
        data->position -= edit.value.len();
        data->line_number -= edit.value.as_str().count('\n');
    } else if (edit.position < data->position) {
        cz::Str str = edit.value.as_str().slice_end(data->position - edit.position);
        data->position -= str.len;
        data->line_number -= str.count('\n');
    }
}

static bool decoration_line_number_append(const Buffer* buffer,
                                          Window_Unified* window,
                                          cz::AllocatedString* string,
                                          void* _data) {
    ZoneScoped;

    Data* data = (Data*)_data;
    Buffer_Data* bd = nullptr;
    for (size_t i = 0; i < data->buffer_datas.len(); ++i) {
        if (data->buffer_datas[i].id == window->id) {
            for (size_t c = data->buffer_datas[i].change_index; c < buffer->changes.len(); ++c) {
                auto& change = buffer->changes[c];
                if (change.is_redo) {
                    for (size_t e = 0; e < change.commit.edits.len; ++e) {
                        if (change.commit.edits[e].flags & Edit::INSERT_MASK) {
                            update_insert(change.commit.edits[e], &data->buffer_datas[i]);
                        } else {
                            update_remove(change.commit.edits[e], &data->buffer_datas[i]);
                        }
                    }
                } else {
                    for (size_t e = change.commit.edits.len; e-- > 0;) {
                        if (change.commit.edits[e].flags & Edit::INSERT_MASK) {
                            update_remove(change.commit.edits[e], &data->buffer_datas[i]);
                        } else {
                            update_insert(change.commit.edits[e], &data->buffer_datas[i]);
                        }
                    }
                }
            }

            data->buffer_datas[i].change_index = buffer->changes.len();

            bd = &data->buffer_datas[i];
            break;
        }
    }

    if (!bd) {
        Buffer_Data buffer_data;
        buffer_data.id = window->id;
        buffer_data.change_index = buffer->changes.len();
        buffer_data.position = 0;
        buffer_data.line_number = 0;

        data->buffer_datas.reserve(cz::heap_allocator(), 1);
        data->buffer_datas.push(buffer_data);

        bd = &data->buffer_datas.last();
    }

    // It's faster to just iterate from the start of the file.
    if (bd->position > window->cursors[0].point * 2) {
        bd->position = 0;
        bd->line_number = 0;
    }

    Contents_Iterator iterator = buffer->contents.iterator_at(bd->position);
    uint64_t max_offset = 200000;
    if (iterator.position < window->cursors[0].point) {
        uint64_t end = std::min(iterator.position + max_offset, window->cursors[0].point);
        while (iterator.position < end) {
            if (iterator.get() == '\n') {
                ++bd->line_number;
            }
            iterator.advance();
        }
    } else if (iterator.position > window->cursors[0].point) {
        uint64_t end = window->cursors[0].point;
        if (end + max_offset < iterator.position) {
            end = iterator.position - max_offset;
        }

        iterator.retreat();
        while (iterator.position >= window->cursors[0].point) {
            if (iterator.get() == '\n') {
                --bd->line_number;
            }
            iterator.retreat();
        }
        iterator.advance();
    }
    bd->position = iterator.position;

    write(string_writer(string), 'L', bd->line_number + 1);

    return true;
}

static void decoration_line_number_cleanup(void* _data) {
    Data* data = (Data*)_data;
    data->buffer_datas.drop(cz::heap_allocator());
    free(data);
}

Decoration decoration_line_number() {
    Data* data = (Data*)calloc(1, sizeof(Data));
    CZ_ASSERT(data);
    static const Decoration::VTable vtable = {decoration_line_number_append,
                                              decoration_line_number_cleanup};
    return {&vtable, data};
}

}
}
