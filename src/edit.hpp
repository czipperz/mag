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

}
