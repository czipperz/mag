#pragma once

#include <cz/string.hpp>
#include <cz/vector.hpp>
#include "message.hpp"

namespace mag {

struct Completion_Results {
    enum State {
        INITIAL,
        LOADING,
        LOADED,
    };

    State state;
    cz::String query;
    cz::Vector<cz::Str> results;
    size_t selected;
    Message::Tag response_tag;
};

void load_completion_results(Completion_Results* completion_results);

}
