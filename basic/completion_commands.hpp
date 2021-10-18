#pragma once

#include "command.hpp"
#include "editor.hpp"

namespace mag {
namespace basic {

void command_insert_completion(Editor* editor, Command_Source source);
void command_insert_completion_and_submit_mini_buffer(Editor* editor, Command_Source source);

void command_next_completion(Editor* editor, Command_Source source);
void command_previous_completion(Editor* editor, Command_Source source);

void command_completion_down_page(Editor* editor, Command_Source source);
void command_completion_up_page(Editor* editor, Command_Source source);

void command_first_completion(Editor* editor, Command_Source source);
void command_last_completion(Editor* editor, Command_Source source);

struct Identifier_Completion_Engine_Data {
    cz::String query;
    cz::Arc_Weak<Buffer_Handle> handle;

    bool load(cz::Allocator allocator, cz::Heap_Vector<cz::Str>* results);
};

bool find_nearest_matching_identifier(Contents_Iterator it,
                                      Contents_Iterator middle,
                                      uint64_t end,
                                      size_t max_buckets,
                                      Contents_Iterator* out);
void command_complete_at_point_nearest_matching(Editor* editor, Command_Source source);

bool find_nearest_matching_identifier_before(Contents_Iterator it,
                                             Contents_Iterator middle,
                                             uint64_t end,
                                             size_t max_buckets,
                                             Contents_Iterator* out);
void command_complete_at_point_nearest_matching_before(Editor* editor, Command_Source source);

bool find_nearest_matching_identifier_after(Contents_Iterator it,
                                            Contents_Iterator middle,
                                            uint64_t end,
                                            size_t max_buckets,
                                            Contents_Iterator* out);
void command_complete_at_point_nearest_matching_after(Editor* editor, Command_Source source);

bool find_nearest_matching_identifier_before_after(Contents_Iterator it,
                                                   Contents_Iterator middle,
                                                   uint64_t end,
                                                   size_t max_buckets,
                                                   Contents_Iterator* out);
void command_complete_at_point_nearest_matching_before_after(Editor* editor, Command_Source source);

void command_complete_at_point_prompt_identifiers(Editor* editor, Command_Source source);

void command_copy_rest_of_line_from_nearest_matching_identifier(Editor* editor,
                                                                Command_Source source);

}
}
