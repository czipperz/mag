#pragma once

#include <cz/defer.hpp>
#include "buffer.hpp"
#include "buffer_handle.hpp"
#include "client.hpp"
#include "ssostr.hpp"
#include "transaction.hpp"

#define WITH_SELECTED_BUFFER(CLIENT)                      \
    Window_Unified* window = (CLIENT)->selected_window(); \
    WITH_WINDOW_BUFFER(window)

#define WITH_WINDOW_BUFFER(WINDOW) \
    WITH_BUFFER((WINDOW)->id);     \
    (WINDOW)->update_cursors(buffer)

#define WITH_BUFFER(BUFFER_ID)                         \
    Buffer_Handle* handle = editor->lookup(BUFFER_ID); \
    Buffer* buffer = handle->lock();                   \
    CZ_DEFER(handle->unlock())

#define TRANSFORM_POINTS(FUNC)                                                           \
    do {                                                                                 \
        cz::Slice<Cursor> cursors = window->cursors;                                     \
        for (size_t i = 0; i < cursors.len; ++i) {                                       \
            Contents_Iterator iterator = buffer->contents.iterator_at(cursors[i].point); \
            FUNC(&iterator);                                                             \
            cursors[i].point = iterator.position;                                        \
        }                                                                                \
    } while (0)

#define DELETE_BACKWARD(FUNC)                                                                   \
    do {                                                                                        \
        cz::Slice<Cursor> cursors = window->cursors;                                            \
        uint64_t sum_regions = 0;                                                               \
        for (size_t c = 0; c < cursors.len; ++c) {                                              \
            Contents_Iterator end = buffer->contents.iterator_at(cursors[c].point);             \
            Contents_Iterator start = end;                                                      \
            FUNC(&start);                                                                       \
            sum_regions += end.position - start.position;                                       \
        }                                                                                       \
                                                                                                \
        Transaction transaction;                                                                \
        transaction.init(cursors.len, (size_t)sum_regions);                                     \
        CZ_DEFER(transaction.drop());                                                           \
                                                                                                \
        uint64_t total = 0;                                                                     \
        for (size_t c = 0; c < cursors.len; ++c) {                                              \
            Contents_Iterator end = buffer->contents.iterator_at(cursors[c].point);             \
            Contents_Iterator start = end;                                                      \
            FUNC(&start);                                                                       \
            if (start.position < end.position) {                                                \
                Edit edit;                                                                      \
                edit.value =                                                                    \
                    buffer->contents.slice(transaction.value_allocator(), start, end.position); \
                edit.position = start.position - total;                                         \
                total += end.position - start.position;                                         \
                edit.flags = Edit::REMOVE;                                                      \
                transaction.push(edit);                                                         \
            }                                                                                   \
        }                                                                                       \
                                                                                                \
        transaction.commit(buffer);                                                             \
    } while (0)

#define DELETE_FORWARD(FUNC)                                                                    \
    do {                                                                                        \
        cz::Slice<Cursor> cursors = window->cursors;                                            \
        uint64_t sum_regions = 0;                                                               \
        for (size_t c = 0; c < cursors.len; ++c) {                                              \
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);           \
            Contents_Iterator end = start;                                                      \
            FUNC(&end);                                                                         \
            sum_regions += end.position - start.position;                                       \
        }                                                                                       \
                                                                                                \
        Transaction transaction;                                                                \
        transaction.init(cursors.len, (size_t)sum_regions);                                     \
        CZ_DEFER(transaction.drop());                                                           \
                                                                                                \
        uint64_t total = 0;                                                                     \
        for (size_t c = 0; c < cursors.len; ++c) {                                              \
            Contents_Iterator start = buffer->contents.iterator_at(cursors[c].point);           \
            Contents_Iterator end = start;                                                      \
            FUNC(&end);                                                                         \
            if (start.position < end.position) {                                                \
                Edit edit;                                                                      \
                edit.value =                                                                    \
                    buffer->contents.slice(transaction.value_allocator(), start, end.position); \
                edit.position = start.position - total;                                         \
                total += end.position - start.position;                                         \
                edit.flags = Edit::REMOVE_AFTER_POSITION;                                       \
                transaction.push(edit);                                                         \
            }                                                                                   \
        }                                                                                       \
                                                                                                \
        transaction.commit(buffer);                                                             \
    } while (0)
