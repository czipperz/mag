#pragma once

#include <cz/str.hpp>
#include "completion.hpp"

namespace mag {

struct Client;
struct Editor;
struct Buffer;

struct Message {
    enum Tag {
        NONE,
        SHOW,
        RESPOND,
    } tag;
    typedef void (*Response_Callback)(Editor*, Client*, cz::Str mini_buffer_contents, void* data);

    cz::Str text;
    Completion_Engine completion_engine;
    Response_Callback response_callback;
    void* response_callback_data;
};

}
