#pragma once

#include "core/ssostr.hpp"

namespace mag {

struct Copy_Chain {
    SSOStr value;
    Copy_Chain* previous;
};

}
