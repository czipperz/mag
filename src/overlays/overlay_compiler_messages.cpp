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
    prose::All_Messages all_messages;

    prose::File_Messages file_messages;
    Client* client;
    prose::Line_And_Column current_line_and_column;
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
    for (size_t i = 0; i < data->all_messages.file_names.len; ++i) {
        if (data->all_messages.file_names[i] == buffer->name) {
            data->file_messages = data->all_messages.file_messages[i];
            break;
        }
    }

    data->client = client;

    Contents_Iterator sol = iterator;
    start_of_line(&sol);
    data->current_line_and_column = {iterator.get_line_number(),
                                     iterator.position - sol.position + 1};

    data->message_index = std::lower_bound(data->file_messages.lines_and_columns.begin(),
                                           data->file_messages.lines_and_columns.end(),
                                           data->current_line_and_column) -
                          data->file_messages.lines_and_columns.begin();

    data->token_it = {};
    data->token_it.init_at_or_after(buffer, iterator.position);
}

static Face overlay_compiler_messages_get_face_and_advance(
    const Buffer*,
    Window_Unified* window,
    Contents_Iterator current_position_iterator,
    void* _data) {
    Data* data = (Data*)_data;
    if (data->message_index >= data->file_messages.lines_and_columns.len) {
        return {};
    }

    CZ_DEFER({
        if (current_position_iterator.get() == '\n') {
            ++data->current_line_and_column.line;
            data->current_line_and_column.column = 1;
        } else {
            ++data->current_line_and_column.column;
        }
    });

    // One message per token.  We want to keep showing the message & overlay until
    // the end of the token thus don't advance the message until that point.
    if (data->token_it.has_token() &&
        !data->token_it.token().contains_position(current_position_iterator.position)) {
        data->token_it.find_at_or_after(current_position_iterator.position);

        while (data->file_messages.lines_and_columns[data->message_index] <
               data->current_line_and_column) {
            ++data->message_index;
            if (data->message_index == data->file_messages.lines_and_columns.len)
                return {};
        }
    }

    if (data->token_it.has_token() &&
        data->token_it.token().contains_position(current_position_iterator.position) &&
        data->current_line_and_column.line ==
            data->file_messages.lines_and_columns[data->message_index].line &&
        data->current_line_and_column.column -
                (current_position_iterator.position - data->token_it.token().start) <=
            data->file_messages.lines_and_columns[data->message_index].column &&
        data->current_line_and_column.column +
                (data->token_it.token().end - current_position_iterator.position) >
            data->file_messages.lines_and_columns[data->message_index].column) {
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
                                                             void* _data) {
    Data* data = (Data*)_data;
    data->current_line_and_column.column += end - start.position;
}

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

bool is_overlay_compiler_messages(const Overlay& overlay) {
    return overlay.vtable == &vtable;
}

void set_overlay_compiler_messages(Overlay* overlay, prose::All_Messages all_messages) {
    CZ_DEBUG_ASSERT(is_overlay_compiler_messages(*overlay));
    Data* data = (Data*)overlay->data;
    data->all_messages = all_messages;
}

}
}
