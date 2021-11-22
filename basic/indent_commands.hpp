#pragma once

#include <stdint.h>

namespace mag {
struct Editor;
struct Command_Source;

struct Buffer;
struct Contents_Iterator;
struct Contents;
struct Transaction;
struct Mode;

namespace basic {

void command_insert_newline_indent(Editor* editor, Command_Source source);
void command_insert_newline_copy_indent_and_modifiers(Editor* editor, Command_Source source);
void command_increase_indent(Editor* editor, Command_Source source);
void command_decrease_indent(Editor* editor, Command_Source source);

void command_delete_whitespace(Editor* editor, Command_Source source);
void command_one_whitespace(Editor* editor, Command_Source source);
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

/// Parse indent rules by attempting to understand the buffer's contents.
/// Returns `false` if there are no indented lines.
///
/// Tries to understand:
/// 1. Space indent
/// 2. Tab indent
/// 3. Space indent with 8-width tabs.
bool parse_indent_rules(const Contents& contents,
                        uint32_t* indent_width,
                        uint32_t* tab_width,
                        bool* use_tabs);

}
}
