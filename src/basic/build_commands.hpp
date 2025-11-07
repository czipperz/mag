#pragma once

#include "core/command.hpp"
#include "core/editor.hpp"

namespace mag {
namespace basic {

void command_build_debug_vc_root(Editor* editor, Command_Source source);

void build_buffer_iterate(Editor* editor, Client* client, bool select_next);
void command_build_next_link(Editor* editor, Command_Source source);
void command_build_previous_link(Editor* editor, Command_Source source);

void command_build_open_link_at_point(Editor* editor, Command_Source source);
void command_build_open_link_at_point_no_swap(Editor* editor, Command_Source source);
void command_build_open_next_link(Editor* editor, Command_Source source);
void command_build_open_next_link_no_swap(Editor* editor, Command_Source source);
void command_build_open_previous_link(Editor* editor, Command_Source source);
void command_build_open_previous_link_no_swap(Editor* editor, Command_Source source);

void command_build_next_file(Editor* editor, Command_Source source);
void command_build_previous_file(Editor* editor, Command_Source source);

void forward_to(Editor* editor, Client* client, Token_Type token_type);
void backward_to(Editor* editor, Client* client, Token_Type token_type);

void command_ctest_next_file(Editor* editor, Command_Source source);
void command_ctest_previous_file(Editor* editor, Command_Source source);
void command_ctest_next_test_case(Editor* editor, Command_Source source);
void command_ctest_previous_test_case(Editor* editor, Command_Source source);

void command_build_next_error(Editor* editor, Command_Source source);
void command_build_previous_error(Editor* editor, Command_Source source);

}
}
