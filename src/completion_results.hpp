#pragma once

#include <cz/string.hpp>
#include <cz/vector.hpp>

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
};

typedef void (*Completion_Engine)(Completion_Results*);
void file_completion_engine(Completion_Results* completion_results);
void buffer_completion_engine(Completion_Results* completion_results);
void no_completion_engine(Completion_Results* completion_results);

}
