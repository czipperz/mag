#pragma once

#include <stdint.h>

namespace mag {
struct Editor;
struct Command_Source;

struct Buffer;
struct Contents_Iterator;
struct Transaction;
struct Mode;

namespace basic {

void command_insert_newline_indent(Editor* editor, Command_Source source);
void command_insert_newline_copy_indent_and_modifiers(Editor* editor, Command_Source source);
void command_increase_indent(Editor* editor, Command_Source source);
void command_decrease_indent(Editor* editor, Command_Source source);

void command_delete_whitespace(Editor* editor, Command_Source source);
void command_merge_lines(Editor* editor, Command_Source source);

uint64_t remove_spaces(Transaction* transaction,
                       const Buffer* buffer,
                       Contents_Iterator it,
                       uint64_t offset);
void insert_line_with_indent(Transaction* transaction,
                             const Mode& mode,
                             uint64_t position,
                             uint64_t* offset,
                             uint64_t columns);

}
}
