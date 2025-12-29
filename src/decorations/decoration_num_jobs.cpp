#include "decoration_num_jobs.hpp"

#include <stdlib.h>
#include <algorithm>
#include <cz/defer.hpp>
#include <cz/format.hpp>
#include <cz/heap.hpp>
#include <cz/vector.hpp>
#include <tracy/Tracy.hpp>
#include "core/buffer.hpp"
#include "core/decoration.hpp"
#include "core/editor.hpp"
#include "core/movement.hpp"
#include "core/visible_region.hpp"
#include "core/window.hpp"

namespace mag {
namespace syntax {

static bool decoration_num_jobs_append(Editor* editor,
                                       Client* client,
                                       const Buffer* buffer,
                                       Window_Unified* window,
                                       cz::Allocator allocator,
                                       cz::String* string,
                                       void* _data) {
    size_t num_uncompleted_async_jobs = editor->num_uncompleted_async_jobs.load();
    if (editor->synchronous_jobs.len == 0 && num_uncompleted_async_jobs == 0) {
        return false;
    }

    cz::append(allocator, string, "Jobs(", editor->synchronous_jobs.len, '/',
               num_uncompleted_async_jobs, ')');
    return true;
}

static void decoration_num_jobs_cleanup(void* _data) {}

Decoration decoration_num_jobs() {
    static const Decoration::VTable vtable = {decoration_num_jobs_append,
                                              decoration_num_jobs_cleanup};
    return {&vtable, nullptr};
}

}
}
