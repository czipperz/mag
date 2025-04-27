#include "hash_commands.hpp"

#include "basic/commands.hpp"
#include "core/command_macros.hpp"
#include "core/editor.hpp"

namespace mag {
namespace hash {

REGISTER_COMMAND(command_insert_divider_60);
void command_insert_divider_60(Editor* editor, Command_Source source) {
    basic::insert_divider_helper(editor, source, '#', 60);
}
REGISTER_COMMAND(command_insert_divider_70);
void command_insert_divider_70(Editor* editor, Command_Source source) {
    basic::insert_divider_helper(editor, source, '#', 70);
}
REGISTER_COMMAND(command_insert_divider_80);
void command_insert_divider_80(Editor* editor, Command_Source source) {
    basic::insert_divider_helper(editor, source, '#', 80);
}
REGISTER_COMMAND(command_insert_divider_90);
void command_insert_divider_90(Editor* editor, Command_Source source) {
    basic::insert_divider_helper(editor, source, '#', 90);
}
REGISTER_COMMAND(command_insert_divider_100);
void command_insert_divider_100(Editor* editor, Command_Source source) {
    basic::insert_divider_helper(editor, source, '#', 100);
}
REGISTER_COMMAND(command_insert_divider_110);
void command_insert_divider_110(Editor* editor, Command_Source source) {
    basic::insert_divider_helper(editor, source, '#', 110);
}
REGISTER_COMMAND(command_insert_divider_120);
void command_insert_divider_120(Editor* editor, Command_Source source) {
    basic::insert_divider_helper(editor, source, '#', 120);
}

REGISTER_COMMAND(command_insert_header_60);
void command_insert_header_60(Editor* editor, Command_Source source) {
    basic::insert_header_helper(editor, source, '#', 1, 60);
}
REGISTER_COMMAND(command_insert_header_70);
void command_insert_header_70(Editor* editor, Command_Source source) {
    basic::insert_header_helper(editor, source, '#', 1, 70);
}
REGISTER_COMMAND(command_insert_header_80);
void command_insert_header_80(Editor* editor, Command_Source source) {
    basic::insert_header_helper(editor, source, '#', 1, 80);
}
REGISTER_COMMAND(command_insert_header_90);
void command_insert_header_90(Editor* editor, Command_Source source) {
    basic::insert_header_helper(editor, source, '#', 1, 90);
}
REGISTER_COMMAND(command_insert_header_100);
void command_insert_header_100(Editor* editor, Command_Source source) {
    basic::insert_header_helper(editor, source, '#', 1, 100);
}
REGISTER_COMMAND(command_insert_header_110);
void command_insert_header_110(Editor* editor, Command_Source source) {
    basic::insert_header_helper(editor, source, '#', 1, 110);
}
REGISTER_COMMAND(command_insert_header_120);
void command_insert_header_120(Editor* editor, Command_Source source) {
    basic::insert_header_helper(editor, source, '#', 1, 120);
}

}
}
