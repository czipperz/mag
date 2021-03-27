#pragma once

#include <cz/vector.hpp>
#include "key.hpp"

namespace mag {

struct Key_Remap {
    struct Key_Transform {
        Key in;
        Key out;
    };

    cz::Vector<Key_Transform> transformations;

    /// Bind a press of the input key to a press of the output key.
    void bind(cz::Str description_in, cz::Str description_out);

    /// Checks if the key is bound.
    bool bound(Key key) const;

    /// Lookup what key the key transforms into.  Undefined behavior if the key is unbound.
    Key get(Key key) const;

    void drop();
};

}
