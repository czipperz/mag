#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cz/vector.hpp>
#include "buffer.hpp"
#include "editor.hpp"
#include "tokenizer_check_point.hpp"
#include "window.hpp"

namespace mag {
namespace client {

struct Window_Cache {
    Window::Tag tag;
    union {
        struct {
            Buffer_Id id;
            size_t change_index;
            uint64_t visible_end;
            cz::Vector<Tokenizer_Check_Point> tokenizer_check_points;
            bool tokenizer_ran_to_end;
        } unified;
        struct {
            Window_Cache* left;
            Window_Cache* right;
        } vertical_split;
        struct {
            Window_Cache* top;
            Window_Cache* bottom;
        } horizontal_split;
    } v;
};

void destroy_window_cache_children(Window_Cache* window_cache);
void destroy_window_cache(Window_Cache* window_cache);
int cache_windows_check_points(Window_Cache* window_cache, Window* w, Editor* editor);
void cache_window_unified_position(Window_Unified* window,
                                   Window_Cache* window_cache,
                                   uint64_t start_position,
                                   Buffer* buffer);
void cache_window_unified_update(Window_Cache* window_cache,
                                 Window_Unified* window,
                                 Buffer* buffer);
void cache_window_unified_create(Editor* editor,
                                 Window_Cache* window_cache,
                                 Window_Unified* window);

}
}
