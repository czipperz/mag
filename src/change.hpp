#pragma once

#include "commit.hpp"

namespace mag {

struct Change {
    Commit commit;
    bool is_redo;

    void adjust_position(uint64_t* position);
};

}
