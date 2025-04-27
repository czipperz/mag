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
    typedef void (*Response_Cancel)(Editor*, Client*, void* data);

    uint64_t start;
    uint64_t end;
    Completion_Engine completion_engine;
    Response_Callback response_callback;
    Response_Callback interactive_response_callback;
    Response_Cancel response_cancel;
    void* response_callback_data;
};

}
