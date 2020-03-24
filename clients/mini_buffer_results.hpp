#pragma once

#include <cz/string.hpp>
#include <cz/vector.hpp>
#include "message.hpp"

namespace mag {
namespace client {

struct Mini_Buffer_Results {
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

void load_mini_buffer_results(Mini_Buffer_Results* mini_buffer_results);

}
}
