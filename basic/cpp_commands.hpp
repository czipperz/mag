#pragma once

namespace mag {
struct Editor;
struct Command_Source;

namespace cpp {

void command_comment(Editor* editor, Command_Source source);

void command_reformat_comment(Editor* editor, Command_Source source);
void command_reformat_comment_block_only(Editor* editor, Command_Source source);

}
}
