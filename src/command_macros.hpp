#pragma once

#include <cz/defer.hpp>
#include "buffer.hpp"
#include "buffer_handle.hpp"
#include "client.hpp"
#include "ssostr.hpp"
#include "transaction.hpp"

#define WITH_SELECTED_BUFFER(CODE) WITH_BUFFER(buffer, source.client->selected_buffer_id(), CODE)

#define WITH_BUFFER(VAR_NAME, BUFFER_ID, CODE)               \
    do {                                                     \
        Buffer_Handle* handle = editor->lookup((BUFFER_ID)); \
        Buffer* VAR_NAME = handle->lock();                   \
        CZ_DEFER(handle->unlock());                          \
        CODE;                                                \
    } while (0)

#define WITH_TRANSACTION(CODE)        \
    do {                              \
        Transaction transaction = {}; \
        CZ_DEFER(transaction.drop()); \
        CODE;                         \
        transaction.commit(buffer);   \
    } while (0)

#define TRANSFORM_POINTS(FUNC)                                                 \
    do {                                                                       \
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {                   \
            buffer->cursors[i].point = FUNC(buffer, buffer->cursors[i].point); \
        }                                                                      \
    } while (0)

#define DELETE_BACKWARD(FUNC)                                                                     \
    do {                                                                                          \
        transaction.reserve(buffer->cursors.len());                                               \
                                                                                                  \
        uint64_t total = 0;                                                                       \
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {                                      \
            uint64_t end = buffer->cursors[i].point;                                              \
            uint64_t start = FUNC(buffer, end);                                                   \
            if (start < end) {                                                                    \
                Edit edit;                                                                        \
                edit.value = buffer->contents.slice(buffer->edit_buffer.allocator(), start, end); \
                edit.position = start - total;                                                    \
                total += end - start;                                                             \
                edit.is_insert = false;                                                           \
                transaction.push(edit);                                                           \
            }                                                                                     \
        }                                                                                         \
    } while (0)

#define DELETE_FORWARD(FUNC)                                                                      \
    do {                                                                                          \
        transaction.reserve(buffer->cursors.len());                                               \
                                                                                                  \
        uint64_t total = 0;                                                                       \
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {                                      \
            uint64_t start = buffer->cursors[i].point;                                            \
            uint64_t end = FUNC(buffer, start);                                                   \
            if (start < end) {                                                                    \
                Edit edit;                                                                        \
                edit.value = buffer->contents.slice(buffer->edit_buffer.allocator(), start, end); \
                edit.position = start - total;                                                    \
                total += end - start;                                                             \
                edit.is_insert = false;                                                           \
                transaction.push(edit);                                                           \
            }                                                                                     \
        }                                                                                         \
    } while (0)

namespace mag {

void insert(Buffer* buffer, char code);
void insert_char(Buffer* buffer, char code);

}
