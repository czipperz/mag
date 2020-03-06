#include "commit.hpp"

#include <stdlib.h>

namespace mag {

void Commit::drop() {
    free((void*)edits.elems);
}

}
