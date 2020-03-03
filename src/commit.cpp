#include "commit.hpp"

#include <stdlib.h>

namespace mag {

void Commit::drop() {
    free(edits.elems);
}

}
