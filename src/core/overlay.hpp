#pragma once

#include <stddef.h>
#include "core/contents.hpp"
#include "core/face.hpp"

namespace mag {

struct Buffer;
struct Editor;
struct Client;
struct Window_Unified;

struct Overlay {
    struct VTable {
        void (*start_frame)(Editor*,
                            Client*,
                            const Buffer*,
                            Window_Unified*,
                            Contents_Iterator start_position_iterator,
                            void*);
        Face (*get_face_and_advance)(const Buffer*,
                                     Window_Unified*,
                                     Contents_Iterator current_position_iterator,
                                     void*);
        Face (*get_face_newline_padding)(const Buffer*,
                                         Window_Unified*,
                                         Contents_Iterator end_of_line_iterator,
                                         void*);
        void (*end_frame)(void*);
        void (*cleanup)(void*);
    };

    const VTable* vtable;
    void* data;

    void start_frame(Editor* editor,
                     Client* client,
                     const Buffer* buffer,
                     Window_Unified* window,
                     Contents_Iterator start_position_iterator) const {
        vtable->start_frame(editor, client, buffer, window, start_position_iterator, data);
    }

    Face get_face_and_advance(const Buffer* buffer,
                              Window_Unified* window,
                              Contents_Iterator iterator) const {
        return vtable->get_face_and_advance(buffer, window, iterator, data);
    }

    Face get_face_newline_padding(const Buffer* buffer,
                                  Window_Unified* window,
                                  Contents_Iterator iterator) const {
        return vtable->get_face_newline_padding(buffer, window, iterator, data);
    }

    void end_frame() const { vtable->end_frame(data); }

    void cleanup() { vtable->cleanup(data); }
};

}
