#include "prose/compiler.hpp"

#include <cz/assert.hpp>
#include <cz/binary_search.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/sort.hpp>
#include <cz/string.hpp>
#include "core/file.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
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

struct All_Messages_Builder {
    cz::Vector<cz::Str> file_names;
    cz::Vector<cz::Vector<File_Message_Builder>> file_messages;

    All_Messages build(cz::Allocator buffer_array_allocator) {
        All_Messages all_messages = {
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

All_Messages parse_errors(Contents_Iterator link_start, cz::Allocator buffer_array_allocator) {
    All_Messages_Builder all_messages_builder = {};
    CZ_DEFER(all_messages_builder.drop());

    cz::String link_storage = {};
    CZ_DEFER(link_storage.drop(cz::heap_allocator()));

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

        cz::Str file;
        uint64_t line, column = -1;
        if (!parse_file_arg_no_disk(link_storage, &file, &line, &column))
            continue;

        size_t index;
        if (!cz::binary_search(all_messages_builder.file_names, file, &index)) {
            all_messages_builder.file_names.reserve(cz::heap_allocator(), 1);
            all_messages_builder.file_names.insert(index, file.clone(buffer_array_allocator));
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

}
}
