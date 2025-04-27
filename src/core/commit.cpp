#include "commit.hpp"

#include <stdlib.h>
#include <cz/heap.hpp>

namespace mag {

void Commit::drop() {
    cz::heap_allocator().dealloc({(void*)edits.elems, 0});
}

}
