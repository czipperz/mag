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

typedef bool (*Completion_Engine)(Editor* editor,
                                  Completion_Engine_Context* context,
                                  bool is_initial_frame);
bool file_completion_engine(Editor*, Completion_Engine_Context*, bool);
bool buffer_completion_engine(Editor*, Completion_Engine_Context*, bool);
bool no_completion_engine(Editor*, Completion_Engine_Context*, bool);

typedef void (*Completion_Filter)(Editor* editor,
                                  Completion_Filter_Context* context,
                                  Completion_Engine_Context* engine_context,
                                  cz::Str selected_result,
                                  bool has_selected_result);
void prefix_completion_filter(Editor* editor,
                              Completion_Filter_Context* context,
                              Completion_Engine_Context* engine_context,
                              cz::Str selected_result,
                              bool has_selected_result);
void infix_completion_filter(Editor* editor,
                             Completion_Filter_Context* context,
                             Completion_Engine_Context* engine_context,
                             cz::Str selected_result,
                             bool has_selected_result);
void spaces_are_wildcards_completion_filter(Editor* editor,
                                            Completion_Filter_Context* context,
                                            Completion_Engine_Context* engine_context,
                                            cz::Str selected_result,
                                            bool has_selected_result);

bool run_command_for_completion_results(Completion_Engine_Context* context,
                                        cz::Slice<cz::Str> args,
                                        cz::Process_Options options,
                                        bool force_reload = false);

struct Completion_Cache {
    Completion_Engine engine;
    Completion_Engine_Context engine_context;

    Completion_Filter_Context filter_context;

    enum {
        INITIAL,
        LOADING,
        LOADED,
    } state;

    size_t change_index;

    void init() { engine_context.init(); }
    void drop() {
        filter_context.drop();
        engine_context.drop();
    }

    bool update(size_t changes_len);
    void set_engine(Completion_Engine new_engine);
};

}
