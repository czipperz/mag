#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace cpp {

void command_comment(Editor* editor, Command_Source source);
void command_comment_line_comments_only(Editor* editor, Command_Source source);
void command_uncomment(Editor* editor, Command_Source source);

void command_reformat_comment(Editor* editor, Command_Source source);
void command_reformat_comment_block_only(Editor* editor, Command_Source source);

/// There are many levels of indirection that can be used to access a variable.
///
/// -3. `(&&&a).b` / `&&&a + b`
/// -2. `(&&a).b`  / `&&a + b`
/// -1. `(&a).b`   / `&a + b`
///  0. `a.b`      / `a + b`
///  1. `a->b`     / `*a + b`
///  2. `(*a)->b`  / `**a + b`
///  3. `(**a)->b` / `***a + b`
/// etc.
///
/// `command_make_direct` decreases the level of indirection
/// whereas `command_make_indirect` increases it.
void command_make_direct(Editor* editor, Command_Source source);
void command_make_indirect(Editor* editor, Command_Source source);

void command_extract_variable(Editor* editor, Command_Source source);

void command_insert_divider_60(Editor* editor, Command_Source source);
void command_insert_divider_70(Editor* editor, Command_Source source);
void command_insert_divider_80(Editor* editor, Command_Source source);

void command_insert_header_60(Editor* editor, Command_Source source);
void command_insert_header_70(Editor* editor, Command_Source source);
void command_insert_header_80(Editor* editor, Command_Source source);

}
}
