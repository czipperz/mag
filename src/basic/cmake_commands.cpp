#include "cmake_commands.hpp"

#include <cz/dedup.hpp>
#include <cz/sort.hpp>
#include "basic/completion_commands.hpp"
#include "core/command_macros.hpp"
#include "core/movement.hpp"

namespace mag {
namespace basic {

static cz::Str cmake_keywords[] = {
    "add_compile_definitions",
    "add_compile_options",
    "add_custom_command",
    "add_custom_target",
    "add_definitions",
    "add_dependencies",
    "add_executable",
    "add_library",
    "add_link_options",
    "add_subdirectory",
    "add_test",
    "aux_source_directory",
    "break",
    "build_command",
    "build_name",
    "cmake_host_system_information",
    "cmake_language",
    "cmake_minimum_required",
    "cmake_parse_arguments",
    "cmake_path",
    "cmake_policy",
    "configure_file",
    "continue",
    "create_test_sourcelist",
    "ctest_build",
    "ctest_configure",
    "ctest_coverage",
    "ctest_empty_binary_directory",
    "ctest_memcheck",
    "ctest_read_custom_files",
    "ctest_run_script",
    "ctest_sleep",
    "ctest_start",
    "ctest_submit",
    "ctest_test",
    "ctest_update",
    "ctest_upload",
    "define_property",
    "else",
    "elseif",
    "enable_language",
    "enable_testing",
    "endforeach",
    "endfunction",
    "endif",
    "endmacro",
    "endwhile",
    "exec_program",
    "execute_process",
    "export",
    "export_library_dependencies",
    "file",
    "find_file",
    "find_library",
    "find_package",
    "find_path",
    "find_program",
    "fltk_wrap_ui",
    "foreach",
    "function",
    "get_cmake_property",
    "get_directory_property",
    "get_filename_component",
    "get_property",
    "get_source_file_property",
    "get_target_property",
    "get_test_property",
    "if",
    "include",
    "include_directories",
    "include_external_msproject",
    "include_guard",
    "include_regular_expression",
    "install",
    "install_files",
    "install_programs",
    "install_targets",
    "link_directories",
    "link_libraries",
    "list",
    "load_cache",
    "load_command",
    "macro",
    "make_directory",
    "mark_as_advanced",
    "math",
    "message",
    "option",
    "output_required_files",
    "project",
    "qt_wrap_cpp",
    "qt_wrap_ui",
    "remove",
    "remove_definitions",
    "return",
    "separate_arguments",
    "set",
    "set_directory_properties",
    "set_property",
    "set_source_files_properties",
    "set_target_properties",
    "set_tests_properties",
    "site_name",
    "source_group",
    "string",
    "subdir_depends",
    "subdirs",
    "target_compile_definitions",
    "target_compile_features",
    "target_compile_options",
    "target_include_directories",
    "target_link_directories",
    "target_link_libraries",
    "target_link_options",
    "target_precompile_headers",
    "target_sources",
    "try_compile",
    "try_run",
    "unset",
    "use_mangled_mesa",
    "utility_source",
    "variable_requires",
    "variable_watch",
    "while",
    "write_file",
};

static bool identifier_or_cmake_completion_engine(Editor* editor,
                                                  Completion_Engine_Context* context,
                                                  bool is_initial_frame) {
    ZoneScoped;
    if (context->results.len > 0) {
        return false;
    }

    context->results_buffer_array.clear();
    context->results.len = 0;

    context->results.reserve(CZ_DIM(cmake_keywords));
    context->results.append(cmake_keywords);

    auto data = (basic::Identifier_Completion_Engine_Data*)context->data;
    if (!data->load(context->results_buffer_array.allocator(), &context->results)) {
        return false;
    }

    cz::sort(context->results);
    cz::dedup(&context->results);

    return true;
}

REGISTER_COMMAND(command_complete_at_point_prompt_identifiers_or_cmake_keywords);
void command_complete_at_point_prompt_identifiers_or_cmake_keywords(Editor* editor,
                                                                    Command_Source source) {
    ZoneScoped;

    WITH_CONST_SELECTED_BUFFER(source.client);

    Contents_Iterator it = buffer->contents.iterator_at(window->cursors[0].point);

    // Retreat to start of identifier.
    Contents_Iterator middle = it;
    backward_through_identifier(&it);

    if (it.position >= middle.position) {
        source.client->show_message("Not at an identifier");
        return;
    }

    basic::Identifier_Completion_Engine_Data* data =
        cz::heap_allocator().alloc<basic::Identifier_Completion_Engine_Data>();
    data->query = {};
    buffer->contents.slice_into(cz::heap_allocator(), it, middle.position, &data->query);
    data->handle = handle.clone_downgrade();

    window->start_completion(identifier_or_cmake_completion_engine);
    window->completion_cache.engine_context.reset();

    window->completion_cache.engine_context.data = data;
    window->completion_cache.engine_context.cleanup = [](void* _data) {
        auto data = (basic::Identifier_Completion_Engine_Data*)_data;
        data->query.drop(cz::heap_allocator());
        data->handle.drop();
        cz::heap_allocator().dealloc(data);
    };
}

}
}
