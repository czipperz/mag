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
        Transaction transaction;      \
        CZ_DEFER(transaction.drop()); \
        CODE;                         \
        transaction.commit(buffer);   \
    } while (0)

#define TRANSFORM_POINTS(FUNC)                                                                   \
    do {                                                                                         \
        for (size_t i = 0; i < buffer->cursors.len(); ++i) {                                     \
            Contents_Iterator iterator = buffer->contents.iterator_at(buffer->cursors[i].point); \
            FUNC(buffer, &iterator);                                                             \
            buffer->cursors[i].point = iterator.position;                                        \
        }                                                                                        \
    } while (0)

#define DELETE_BACKWARD(FUNC)                                                                      \
    do {                                                                                           \
        uint64_t sum_regions = 0;                                                                  \
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {                                       \
            Contents_Iterator end = buffer->contents.iterator_at(buffer->cursors[c].point);        \
            Contents_Iterator start = end;                                                         \
            FUNC(buffer, &start);                                                                  \
            sum_regions += end.position - start.position;                                          \
        }                                                                                          \
                                                                                                   \
        transaction.init(buffer->cursors.len(), (size_t)sum_regions);                              \
                                                                                                   \
        uint64_t total = 0;                                                                        \
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {                                       \
            Contents_Iterator end = buffer->contents.iterator_at(buffer->cursors[c].point);        \
            Contents_Iterator start = end;                                                         \
            FUNC(buffer, &start);                                                                  \
            if (start.position < end.position) {                                                   \
                Edit edit;                                                                         \
                edit.value = buffer->contents.slice(transaction.value_allocator(), start.position, \
                                                    end.position);                                 \
                edit.position = start.position - total;                                            \
                total += end.position - start.position;                                            \
                edit.is_insert = false;                                                            \
                transaction.push(edit);                                                            \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define DELETE_FORWARD(FUNC)                                                                       \
    do {                                                                                           \
        uint64_t sum_regions = 0;                                                                  \
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {                                       \
            Contents_Iterator start = buffer->contents.iterator_at(buffer->cursors[c].point);      \
            Contents_Iterator end = start;                                                         \
            FUNC(buffer, &end);                                                                    \
            sum_regions += end.position - start.position;                                          \
        }                                                                                          \
                                                                                                   \
        transaction.init(buffer->cursors.len(), (size_t)sum_regions);                              \
                                                                                                   \
        uint64_t total = 0;                                                                        \
        for (size_t c = 0; c < buffer->cursors.len(); ++c) {                                       \
            Contents_Iterator start = buffer->contents.iterator_at(buffer->cursors[c].point);      \
            Contents_Iterator end = start;                                                         \
            FUNC(buffer, &end);                                                                    \
            if (start.position < end.position) {                                                   \
                Edit edit;                                                                         \
                edit.value = buffer->contents.slice(transaction.value_allocator(), start.position, \
                                                    end.position);                                 \
                edit.position = start.position - total;                                            \
                total += end.position - start.position;                                            \
                edit.is_insert = false;                                                            \
                transaction.push(edit);                                                            \
            }                                                                                      \
        }                                                                                          \
    } while (0)

namespace mag {

void insert(Buffer* buffer, char code);
void insert_char(Buffer* buffer, char code);

}
