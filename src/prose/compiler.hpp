#pragma once

#include <stdint.h>
#include <cz/allocator.hpp>
#include <cz/slice.hpp>
#include <cz/str.hpp>
#include "core/contents.hpp"

namespace mag {
struct Command_Source;
struct Editor;
struct Overlay;

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

struct All_Messages {
    cz::Slice<cz::Str> file_names;
    cz::Slice<File_Messages> file_messages;
};

All_Messages parse_errors(Contents_Iterator it, cz::Allocator buffer_array_allocator);

extern Overlay* overlay_compiler_messages;
void command_load_global_compiler_messages(Editor* editor, Command_Source source);

}
}
