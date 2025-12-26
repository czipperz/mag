#include "prose/compiler.hpp"

#include <algorithm>
#include <cz/assert.hpp>
#include <cz/binary_search.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/path.hpp>
#include <cz/sort.hpp>
#include <cz/string.hpp>
#include "core/command_macros.hpp"
#include "core/file.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "prose/open_relpath.hpp"
#include "syntax/tokenize_build.hpp"

namespace mag {
namespace prose {

bool Line_And_Column::operator==(const Line_And_Column& other) const {
    return line == other.line && column == other.column;
}
bool Line_And_Column::operator<(const Line_And_Column& other) const {
    if (line != other.line)
        return line < other.line;
    return column < other.column;
}

static bool next_link(Contents_Iterator* it, Contents_Iterator* link_end) {
    while (1) {
        *link_end = *it;
        if (syntax::build_eat_link(link_end))
            return true;
        end_of_line(it);
        forward_char(it);
        if (it->at_eob())
            return false;
    }
}

namespace {
struct File_Message_Builder {
    uint64_t line;
    uint64_t column;
    cz::Str message;

    bool operator<(const File_Message_Builder& other) const {
        if (line != other.line)
            return line < other.line;
        return column < other.column;
    }
};

struct Buffer_Messages_Builder {
    cz::Vector<cz::Str> file_names;
    cz::Vector<cz::Vector<File_Message_Builder>> file_messages;

    Buffer_Messages build(cz::Allocator buffer_array_allocator) {
        Buffer_Messages all_messages = {
            buffer_array_allocator.alloc_slice<cz::Str>(this->file_names.len),
            buffer_array_allocator.alloc_slice<File_Messages>(this->file_messages.len),
        };

        for (size_t i = 0; i < this->file_messages.len; ++i) {
            cz::Slice<File_Message_Builder> file_messages = this->file_messages[i];
            cz::sort(file_messages);

            all_messages.file_names[i] = this->file_names[i];
            all_messages.file_messages[i] = {
                buffer_array_allocator.alloc_slice<Line_And_Column>(file_messages.len),
                buffer_array_allocator.alloc_slice<cz::Str>(file_messages.len)};
            for (size_t j = 0; j < file_messages.len; ++j) {
                all_messages.file_messages[i].lines_and_columns[j] = {file_messages[j].line,
                                                                      file_messages[j].column};
                all_messages.file_messages[i].messages[j] = file_messages[j].message;
            }
        }

        return all_messages;
    }

    void drop() {
        file_names.drop(cz::heap_allocator());
        for (size_t i = 0; i < file_messages.len; ++i) {
            file_messages[i].drop(cz::heap_allocator());
        }
        file_messages.drop(cz::heap_allocator());
    }
};
}

Buffer_Messages parse_messages(Contents_Iterator link_start,
                          cz::Str directory,
                          cz::Allocator buffer_array_allocator) {
    Buffer_Messages_Builder all_messages_builder = {};
    CZ_DEFER(all_messages_builder.drop());

    cz::String link_storage = {};
    CZ_DEFER(link_storage.drop(cz::heap_allocator()));
    cz::String absolute_path_storage = {};
    CZ_DEFER(absolute_path_storage.drop(cz::heap_allocator()));
    cz::String vc_root = {};
    CZ_DEFER(vc_root.drop(cz::heap_allocator()));

    for (Contents_Iterator link_end; next_link(&link_start, &link_end);
         end_of_line(&link_start), forward_char(&link_start)) {
        Contents_Iterator message_start = link_end;
        if (!looking_at(message_start, ": "))
            continue;
        message_start.advance(2);
        Contents_Iterator message_end = message_start;
        end_of_line(&message_end);

        // src/syntax/tokenize_build.cpp:75:23: error: no matching function ...
        // [--- link -------------------------) [--- message ------------------)
        // [--- file -------------------)[-)[-)
        //                          line -^  ^- column

        link_storage.len = 0;
        link_start.contents->slice_into(cz::heap_allocator(), link_start, link_end.position,
                                        &link_storage);

        cz::Str path;
        uint64_t line, column = -1;
        if (!parse_file_arg_no_disk(link_storage, &path, &line, &column))
            continue;

        if (directory.len > 0) {
            absolute_path_storage.len = 0;
            vc_root.len = 0;
            if (!get_relpath(directory, path, cz::heap_allocator(), &absolute_path_storage,
                             &vc_root)) {
                continue;
            }
            path = absolute_path_storage;
        }

        size_t index;
        if (!cz::binary_search(all_messages_builder.file_names, path, &index)) {
            all_messages_builder.file_names.reserve(cz::heap_allocator(), 1);
            all_messages_builder.file_names.insert(index, path.clone(buffer_array_allocator));
            all_messages_builder.file_messages.reserve(cz::heap_allocator(), 1);
            all_messages_builder.file_messages.insert(index, {});
        }

        all_messages_builder.file_messages[index].reserve(cz::heap_allocator(), 1);
        all_messages_builder.file_messages[index].push(
            {line, column,
             message_start.contents->slice_str(buffer_array_allocator, message_start,
                                               message_end.position)});
    }

    return all_messages_builder.build(buffer_array_allocator);
}

struct Buffer_State {
    cz::Arc_Weak<Buffer_Handle> buffer_handle;
    cz::Buffer_Array buffer_array;
    Buffer_Messages messages;
};
static cz::Vector<Buffer_State> buffer_states;

File_Messages get_file_messages(cz::Str path) {
    for (size_t i = buffer_states.len; i-- > 0;) {
        Buffer_Messages messages = buffer_states[i].messages;
        for (size_t j = 0; j < messages.file_names.len; ++j) {
            if (messages.file_names[j] == path) {
                return messages.file_messages[j];
            }
        }
    }
    return {};
}

REGISTER_COMMAND(command_install_compiler_messages);
void command_install_compiler_messages(Editor* editor, Command_Source source) {
    WITH_CONST_SELECTED_BUFFER(source.client);
    install_messages(buffer, handle);
}

void install_messages(const Buffer* buffer, const cz::Arc<Buffer_Handle>& buffer_handle) {
    // Parsing the file is only done periodically, clean up dead buffers.
    for (size_t i = 0; i < buffer_states.len; ++i) {
        if (!buffer_states[i].buffer_handle.still_alive()) {
            buffer_states[i].buffer_handle.drop();
            buffer_states[i].buffer_array.drop();
            buffer_states.remove(i);
        }
    }

    size_t i = 0;
    for (; i < buffer_states.len; ++i) {
        if (buffer_states[i].buffer_handle.ptr_equal(buffer_handle))
            break;
    }
    if (i == buffer_states.len) {
        buffer_states.reserve(cz::heap_allocator(), 1);
        Buffer_State state = {};
        state.buffer_handle = buffer_handle.clone_downgrade();
        state.buffer_array.init();
        buffer_states.push(state);
    } else {
        // Reorder the installed state last so it is found first in the loop above.
        std::rotate(buffer_states.begin() + i, buffer_states.begin() + i + 1, buffer_states.end());
    }

    Buffer_State* state = &buffer_states.last();
    state->buffer_array.clear();
    state->messages =
        parse_messages(buffer->contents.start(), buffer->directory, state->buffer_array.allocator());
}

}
}
