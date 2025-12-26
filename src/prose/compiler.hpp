#pragma once

#include <stdint.h>
#include <cz/allocator.hpp>
#include <cz/arc.hpp>
#include <cz/slice.hpp>
#include <cz/str.hpp>
#include "core/contents.hpp"

namespace mag {
struct Buffer;
struct Buffer_Handle;
struct Command_Source;
struct Editor;

namespace prose {

struct Line_And_Column {
    uint64_t line;
    uint64_t column;

    bool operator==(const Line_And_Column&) const;
    bool operator<(const Line_And_Column&) const;
};

struct File_Messages {
    cz::Slice<Line_And_Column> lines_and_columns;
    cz::Slice<cz::Str> messages;
};

struct Buffer_Messages {
    cz::Slice<cz::Str> file_names;
    cz::Slice<File_Messages> file_messages;
};

Buffer_Messages parse_messages(Contents_Iterator it,
                               cz::Str directory,
                               cz::Allocator buffer_array_allocator);

File_Messages get_file_messages(cz::Str path);

void command_install_compiler_messages(Editor* editor, Command_Source source);

void install_messages(const Buffer* buffer, const cz::Arc<Buffer_Handle>& buffer_handle);

}
}
