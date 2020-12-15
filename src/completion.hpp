#pragma once

#include <cz/buffer_array.hpp>
#include <cz/string.hpp>
#include <cz/vector.hpp>

namespace cz {
struct Process_Options;
}

namespace mag {
struct Editor;

struct Completion_Filter_Context {
    cz::Vector<cz::Str> results;
    size_t selected;

    void* data;
    void (*cleanup)(void* data);

    void reset();
    void drop();
};

struct Completion_Engine_Context {
    cz::String query;

    cz::Buffer_Array results_buffer_array;
    cz::Vector<cz::Str> results;

    void* data;
    void (*cleanup)(void* data);

    void init() { results_buffer_array.create(); }
    void drop();
};

typedef void (*Completion_Engine)(Editor* editor, Completion_Engine_Context*);
void file_completion_engine(Editor*, Completion_Engine_Context*);
void buffer_completion_engine(Editor*, Completion_Engine_Context*);
void no_completion_engine(Editor*, Completion_Engine_Context*);

typedef void (*Completion_Filter)(Completion_Filter_Context*,
                                  Completion_Engine,
                                  Editor*,
                                  Completion_Engine_Context*);
void prefix_completion_filter(Completion_Filter_Context*,
                              Completion_Engine,
                              Editor*,
                              Completion_Engine_Context*);
void infix_completion_filter(Completion_Filter_Context*,
                             Completion_Engine,
                             Editor*,
                             Completion_Engine_Context*);

void run_command_for_completion_results(Completion_Engine_Context* context,
                                        cz::Slice<cz::Str> args,
                                        cz::Process_Options options);
struct Completion_Cache {
    Completion_Engine engine;
    Completion_Engine_Context engine_context;

    Completion_Filter_Context filter_context;

    enum State {
        INITIAL,
        LOADING,
        LOADED,
    };
    State state;
    size_t change_index;

    void init() { engine_context.init(); }
    void drop() {
        filter_context.drop();
        engine_context.drop();
    }
};

}
