#pragma once

#include <cz/defer.hpp>
#include "core/buffer.hpp"
#include "core/buffer_handle.hpp"
#include "core/client.hpp"
#include "core/ssostr.hpp"
#include "core/transaction.hpp"

///////////////////////////////////////////////////////////////////////////////
// Register a command
///////////////////////////////////////////////////////////////////////////////

#define REGISTER_COMMAND(FUNC) Command_Registrar CZ_TOKEN_CONCAT(register_, FUNC)(COMMAND(FUNC))

///////////////////////////////////////////////////////////////////////////////
// Macros for locking a file in read/write mode
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
// Macros for locking a file in read/only mode
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
// Utility function for movement wrapper commands
///////////////////////////////////////////////////////////////////////////////

#define TRANSFORM_POINTS(FUNC)                                                           \
    do {                                                                                 \
        cz::Slice<Cursor> cursors = window->cursors;                                     \
        for (size_t i = 0; i < cursors.len; ++i) {                                       \
            Contents_Iterator iterator = buffer->contents.iterator_at(cursors[i].point); \
            FUNC(&iterator);                                                             \
            cursors[i].point = iterator.position;                                        \
        }                                                                                \
    } while (0)

///////////////////////////////////////////////////////////////////////////////
// Utility function for deletion + movement wrapper commands
///////////////////////////////////////////////////////////////////////////////

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
                edit.flags = Edit::REMOVE;                                                      \
                transaction.push(edit);                                                         \
                                                                                                \
                if (editor->theme.insert_replace && !edit.value.as_str().contains('\n')) {      \
                    Edit insert;                                                                \
                    if (end.position - start.position <= SSOStr::MAX_SHORT_LEN) {               \
                        char spaces[SSOStr::MAX_SHORT_LEN];                                     \
                        memset(spaces, ' ', end.position - start.position);                     \
                        insert.value =                                                          \
                            SSOStr::from_constant({spaces, end.position - start.position});     \
                    } else {                                                                    \
                        insert.value = SSOStr::from_constant(                                   \
                            cz::format(transaction.value_allocator(),                           \
                                       cz::many(' ', end.position - start.position)));          \
                    }                                                                           \
                    insert.position = start.position;                                           \
                    insert.flags = Edit::INSERT_AFTER_POSITION;                                 \
                    transaction.push(insert);                                                   \
                } else {                                                                        \
                    offset += end.position - start.position;                                    \
                }                                                                               \
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
