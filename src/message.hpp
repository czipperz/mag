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
    } tag;

    cz::Str text;
    void (*response_callback)(Editor*, Client*, cz::Str mini_buffer_contents, void* data);
    void* response_callback_data;
};

}
