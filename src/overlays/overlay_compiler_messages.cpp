#include "overlays/overlay_compiler_messages.hpp"
#include <algorithm>
#include <cz/assert.hpp>
#include <cz/heap.hpp>
#include "core/client.hpp"
#include "core/movement.hpp"
#include "core/token_iterator.hpp"

namespace mag {
namespace syntax {

namespace {
struct Data {
    // Per-window transient state.
    prose::File_Messages file_messages;
    Client* client;
    uint64_t message_index;
    Forward_Token_Iterator token_it;
};
}

static void overlay_compiler_messages_start_frame(Editor*,
                                                  Client* client,
                                                  const Buffer* buffer,
                                                  Window_Unified* window,
                                                  Contents_Iterator iterator,
                                                  void* _data) {
    Data* data = (Data*)_data;

    data->file_messages = {};

    if (buffer->type != Buffer::FILE)
        return;

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    path.reserve_exact(cz::heap_allocator(), buffer->directory.len + buffer->name.len);
    path.append(buffer->directory);
    path.append(buffer->name);
    data->file_messages = prose::get_file_messages(buffer, path);

    data->client = client;

    data->message_index =
        std::lower_bound(data->file_messages.resolved_positions.begin(),
                         data->file_messages.resolved_positions.end(), iterator.position) -
        data->file_messages.resolved_positions.begin();

    data->token_it = {};
    data->token_it.init_at_or_after(buffer, iterator.position);
}

static Face overlay_compiler_messages_get_face_and_advance(
    const Buffer*,
    Window_Unified* window,
    Contents_Iterator current_position_iterator,
    void* _data) {
    Data* data = (Data*)_data;
    if (data->message_index >= data->file_messages.resolved_positions.len) {
        return {};
    }

    // One message per token.  We want to keep showing the message & overlay until
    // the end of the token thus don't advance the message until that point.
    if (data->token_it.has_token() &&
        !data->token_it.token().contains_position(current_position_iterator.position)) {
        data->token_it.find_at_or_after(current_position_iterator.position);

        while (data->file_messages.resolved_positions[data->message_index] <
               current_position_iterator.position) {
            ++data->message_index;
            if (data->message_index == data->file_messages.resolved_positions.len)
                return {};
        }
    }

    if (data->token_it.has_token() &&
        data->token_it.token().contains_position(current_position_iterator.position) &&
        data->token_it.token().contains_position(
            data->file_messages.resolved_positions[data->message_index])) {
        if (data->client->_message.tag == Message::NONE &&
            data->client->selected_window() == window &&
            window->sel().point == current_position_iterator.position) {
            data->client->show_message(data->file_messages.messages[data->message_index]);
        }
        return {{}, {}, Face::UNDERSCORE};
    }

    return {};
}

static Face overlay_compiler_messages_get_face_newline_padding(
    const Buffer*,
    Window_Unified*,
    Contents_Iterator end_of_line_iterator,
    void*) {
    return {};
}

static void overlay_compiler_messages_skip_forward_same_line(const Buffer*,
                                                             Window_Unified*,
                                                             Contents_Iterator start,
                                                             uint64_t end,
                                                             void* _data) {}

static void overlay_compiler_messages_end_frame(void*) {}

static void overlay_compiler_messages_cleanup(void* _data) {
    Data* data = (Data*)_data;
    cz::heap_allocator().dealloc(data);
}

static Overlay::VTable vtable = {
    overlay_compiler_messages_start_frame,
    overlay_compiler_messages_get_face_and_advance,
    overlay_compiler_messages_get_face_newline_padding,
    overlay_compiler_messages_skip_forward_same_line,
    overlay_compiler_messages_end_frame,
    overlay_compiler_messages_cleanup,
};

Overlay overlay_compiler_messages() {
    Data* data = cz::heap_allocator().alloc<Data>();
    CZ_ASSERT(data);
    *data = {};
    return {&vtable, data};
}

}
}
