#pragma once

#include <cz/defer.hpp>
#include "buffer.hpp"
#include "buffer_handle.hpp"
#include "client.hpp"
#include "ssostr.hpp"
#include "transaction.hpp"

#define WITH_SELECTED_NORMAL_BUFFER(CLIENT)                    \
    Window_Unified* window = (CLIENT)->selected_normal_window; \
    WITH_WINDOW_BUFFER(window)

#define WITH_SELECTED_BUFFER(CLIENT)                      \
    Window_Unified* window = (CLIENT)->selected_window(); \
    WITH_WINDOW_BUFFER(window)

#define WITH_WINDOW_BUFFER(WINDOW)                           \
    cz::Arc<Buffer_Handle> handle = (WINDOW)->buffer_handle; \
    WITH_BUFFER_HANDLE(handle);                              \
    (WINDOW)->update_cursors(buffer)

#define WITH_BUFFER_HANDLE(HANDLE)             \
    Buffer* buffer = (HANDLE)->lock_writing(); \
    CZ_DEFER((HANDLE)->unlock())

#define WITH_CONST_SELECTED_NORMAL_BUFFER(CLIENT)              \
    Window_Unified* window = (CLIENT)->selected_normal_window; \
    WITH_CONST_WINDOW_BUFFER(window)

#define WITH_CONST_SELECTED_BUFFER(CLIENT)                \
    Window_Unified* window = (CLIENT)->selected_window(); \
    WITH_CONST_WINDOW_BUFFER(window)

#define WITH_CONST_WINDOW_BUFFER(WINDOW)                     \
    cz::Arc<Buffer_Handle> handle = (WINDOW)->buffer_handle; \
    WITH_CONST_BUFFER_HANDLE(handle);                        \
    (WINDOW)->update_cursors(buffer)

#define WITH_CONST_BUFFER_HANDLE(HANDLE)             \
    const Buffer* buffer = (HANDLE)->lock_reading(); \
    CZ_DEFER((HANDLE)->unlock())

#define TRANSFORM_POINTS(FUNC)                                                           \
    do {                                                                                 \
        cz::Slice<Cursor> cursors = window->cursors;                                     \
        for (size_t i = 0; i < cursors.len; ++i) {                                       \
            Contents_Iterator iterator = buffer->contents.iterator_at(cursors[i].point); \
            FUNC(&iterator);                                                             \
            cursors[i].point = iterator.position;                                        \
        }                                                                                \
    } while (0)

#define DELETE_BACKWARD(FUNC, COMMITTER)                                                        \
    do {                                                                                        \
        Transaction transaction;                                                                \
        transaction.init(buffer);                                                               \
        CZ_DEFER(transaction.drop());                                                           \
                                                                                                \
        cz::Slice<Cursor> cursors = window->cursors;                                            \
        uint64_t offset = 0;                                                                    \
        for (size_t c = 0; c < cursors.len; ++c) {                                              \
            Contents_Iterator end = buffer->contents.iterator_at(cursors[c].point);             \
            Contents_Iterator start = end;                                                      \
            FUNC(&start);                                                                       \
            if (start.position < end.position) {                                                \
                Edit edit;                                                                      \
                edit.value =                                                                    \
                    buffer->contents.slice(transaction.value_allocator(), start, end.position); \
                edit.position = start.position - offset;                                        \
                offset += end.position - start.position;                                        \
                edit.flags = Edit::REMOVE;                                                      \
                transaction.push(edit);                                                         \
            }                                                                                   \
        }                                                                                       \
                                                                                                \
        transaction.commit(source.client, COMMITTER);                                           \
    } while (0)

#define DELETE_FORWARD(FUNC, COMMITTER)                                                         \
    do {                                                                                        \
        Transaction transaction;                                                                \
        transaction.init(buffer);                                                               \
        CZ_DEFER(transaction.drop());                                                           \
                                                                                                \
        cz::Slice<Cursor> cursors = window->cursors;                                            \
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
        transaction.commit(source.client, COMMITTER);                                           \
    } while (0)
