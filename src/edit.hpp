#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ssostr.hpp"

namespace mag {

struct Edit {
    SSOStr value;
    uint64_t position;
    bool is_insert;
};

void position_after_edits(cz::Slice<const Edit> edits, uint64_t* position);
void position_before_edits(cz::Slice<const Edit> edits, uint64_t* position);

}
