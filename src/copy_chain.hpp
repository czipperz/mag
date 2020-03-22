#pragma once

#include "ssostr.hpp"

namespace mag {

struct Copy_Chain {
    SSOStr value;
    Copy_Chain* previous;
};

}
