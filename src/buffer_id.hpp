#pragma once

#include <stdint.h>

namespace mag {

struct Buffer_Id {
    uint64_t value;

    bool operator==(const Buffer_Id& other) const {
        return value == other.value;
    }
};

}
