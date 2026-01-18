#include "decoration_column_number.hpp"

#include <stdlib.h>
#include <algorithm>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/decoration.hpp"
#include "core/movement.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

static bool decoration_column_number_append(Editor*,
                                            Client*,
                                            const Buffer* buffer,
                                            Window_Unified* window,
                                            cz::Allocator allocator,
                                            cz::String* string,
                                            void* _data) {
    Cursor cursor = window->cursors[window->selected_cursor];
    if (window->show_marks) {
        Contents_Iterator start_sol = buffer->contents.iterator_at(cursor.start());
        start_of_line(&start_sol);
        Contents_Iterator end_sol = start_sol;
        end_sol.advance_to(cursor.end());
        start_of_line(&end_sol);
        cz::append(allocator, string, 'C', cursor.start() - start_sol.position + 1, '-',
                   cursor.end() - end_sol.position + 1);
    } else {
        Contents_Iterator sol = buffer->contents.iterator_at(cursor.point);
        start_of_line(&sol);
        cz::append(allocator, string, 'C', cursor.point - sol.position + 1);
    }
    return true;
}

static void decoration_column_number_cleanup(void* _data) {}

Decoration decoration_column_number() {
    static const Decoration::VTable vtable = {decoration_column_number_append,
                                              decoration_column_number_cleanup};
    return {&vtable, nullptr};
}

}
}
