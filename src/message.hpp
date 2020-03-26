#pragma once

#include <cz/str.hpp>

namespace mag {

struct Client;
struct Editor;
struct Buffer;

struct Message {
    enum Tag {
        NONE,
        SHOW,
        RESPOND_TEXT,
        // RESPOND_YES_NO,
        RESPOND_FILE,
        RESPOND_BUFFER,
    } tag;
    typedef void (*Response_Callback)(Editor*, Client*, cz::Str mini_buffer_contents, void* data);

    cz::Str text;
    Response_Callback response_callback;
    void* response_callback_data;
};

}
