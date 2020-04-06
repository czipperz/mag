#pragma once

#include <cz/buffer_array.hpp>
#include <cz/string.hpp>
#include <cz/vector.hpp>

namespace mag {
struct Editor;

struct Completion_Results {
    enum State {
        INITIAL,
        LOADING,
        LOADED,
    };

    State state;
    cz::String query;
    cz::Buffer_Array results_buffer_array;

    cz::Vector<cz::Str> results;
    size_t selected;

    // Extra data used by the completion engine.
    void* data;
    void (*cleanup)(void* data);

    void init() { results_buffer_array.create(); }

    void drop();
};

bool binary_search_string_prefix_start(cz::Slice<cz::Str> results, cz::Str prefix, size_t* out);
size_t binary_search_string_prefix_end(cz::Slice<cz::Str> results, size_t start, cz::Str prefix);

typedef void (*Completion_Engine)(Editor* editor, Completion_Results*);
void file_completion_engine(Editor* _editor, Completion_Results* completion_results);
void buffer_completion_engine(Editor* editor, Completion_Results* completion_results);
void no_completion_engine(Editor* _editor, Completion_Results* completion_results);

struct Completion_Cache {
    Completion_Results results;
    size_t change_index;
    Completion_Engine engine;

    void init() { results.init(); }
    void drop() { results.drop(); }
};

}
