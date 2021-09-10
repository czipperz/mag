#include "tokenize_cmake.hpp"

#include <Tracy.hpp>
#include <cz/char_type.hpp>
#include "common.hpp"
#include "contents.hpp"
#include "face.hpp"
#include "match.hpp"
#include "movement.hpp"
#include "token.hpp"

namespace mag {
namespace syntax {

static bool is_special(char ch) {
    return ch == '"' || ch == '\'' || ch == '$' || ch == '{' || ch == '[' || ch == '(' ||
           ch == '}' || ch == ']' || ch == ')' || ch == '#' || cz::is_space(ch);
}

static bool looking_at_keyword(Contents_Iterator start, uint64_t length) {
    switch (length) {
    case 2:
        if (looking_at_no_bounds_check(start, "if"))
            return true;
        return false;

    case 3:
        if (looking_at_no_bounds_check(start, "set"))
            return true;
        return false;

    case 4:
        if (looking_at_no_bounds_check(start, "else"))
            return true;
        if (looking_at_no_bounds_check(start, "file"))
            return true;
        if (looking_at_no_bounds_check(start, "list"))
            return true;
        if (looking_at_no_bounds_check(start, "math"))
            return true;
        return false;

    case 5:
        if (looking_at_no_bounds_check(start, "break"))
            return true;
        if (looking_at_no_bounds_check(start, "endif"))
            return true;
        if (looking_at_no_bounds_check(start, "macro"))
            return true;
        if (looking_at_no_bounds_check(start, "unset"))
            return true;
        if (looking_at_no_bounds_check(start, "while"))
            return true;
        return false;

    case 6:
        if (looking_at_no_bounds_check(start, "elseif"))
            return true;
        if (looking_at_no_bounds_check(start, "export"))
            return true;
        if (looking_at_no_bounds_check(start, "option"))
            return true;
        if (looking_at_no_bounds_check(start, "remove"))
            return true;
        if (looking_at_no_bounds_check(start, "return"))
            return true;
        if (looking_at_no_bounds_check(start, "string"))
            return true;
        return false;

    case 7:
        if (looking_at_no_bounds_check(start, "foreach"))
            return true;
        if (looking_at_no_bounds_check(start, "include"))
            return true;
        if (looking_at_no_bounds_check(start, "install"))
            return true;
        if (looking_at_no_bounds_check(start, "message"))
            return true;
        if (looking_at_no_bounds_check(start, "project"))
            return true;
        if (looking_at_no_bounds_check(start, "subdirs"))
            return true;
        if (looking_at_no_bounds_check(start, "try_run"))
            return true;
        return false;

    case 8:
        if (looking_at_no_bounds_check(start, "add_test"))
            return true;
        if (looking_at_no_bounds_check(start, "continue"))
            return true;
        if (looking_at_no_bounds_check(start, "endmacro"))
            return true;
        if (looking_at_no_bounds_check(start, "endwhile"))
            return true;
        if (looking_at_no_bounds_check(start, "function"))
            return true;
        return false;

    case 9:
        if (looking_at_no_bounds_check(start, "find_file"))
            return true;
        if (looking_at_no_bounds_check(start, "find_path"))
            return true;
        if (looking_at_no_bounds_check(start, "site_name"))
            return true;
        return false;

    case 10:
        if (looking_at_no_bounds_check(start, "build_name"))
            return true;
        if (looking_at_no_bounds_check(start, "cmake_path"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_test"))
            return true;
        if (looking_at_no_bounds_check(start, "endforeach"))
            return true;
        if (looking_at_no_bounds_check(start, "load_cache"))
            return true;
        if (looking_at_no_bounds_check(start, "qt_wrap_ui"))
            return true;
        if (looking_at_no_bounds_check(start, "write_file"))
            return true;
        return false;

    case 11:
        if (looking_at_no_bounds_check(start, "add_library"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_build"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_sleep"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_start"))
            return true;
        if (looking_at_no_bounds_check(start, "endfunction"))
            return true;
        if (looking_at_no_bounds_check(start, "qt_wrap_cpp"))
            return true;
        if (looking_at_no_bounds_check(start, "try_compile"))
            return true;
        return false;

    case 12:
        if (looking_at_no_bounds_check(start, "cmake_policy"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_submit"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_update"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_upload"))
            return true;
        if (looking_at_no_bounds_check(start, "exec_program"))
            return true;
        if (looking_at_no_bounds_check(start, "find_library"))
            return true;
        if (looking_at_no_bounds_check(start, "find_package"))
            return true;
        if (looking_at_no_bounds_check(start, "find_program"))
            return true;
        if (looking_at_no_bounds_check(start, "fltk_wrap_ui"))
            return true;
        if (looking_at_no_bounds_check(start, "get_property"))
            return true;
        if (looking_at_no_bounds_check(start, "load_command"))
            return true;
        if (looking_at_no_bounds_check(start, "set_property"))
            return true;
        if (looking_at_no_bounds_check(start, "source_group"))
            return true;
        return false;

    case 13:
        if (looking_at_no_bounds_check(start, "build_command"))
            return true;
        if (looking_at_no_bounds_check(start, "include_guard"))
            return true;
        if (looking_at_no_bounds_check(start, "install_files"))
            return true;
        return false;

    case 14:
        if (looking_at_no_bounds_check(start, "add_executable"))
            return true;
        if (looking_at_no_bounds_check(start, "cmake_language"))
            return true;
        if (looking_at_no_bounds_check(start, "configure_file"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_coverage"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_memcheck"))
            return true;
        if (looking_at_no_bounds_check(start, "enable_testing"))
            return true;
        if (looking_at_no_bounds_check(start, "link_libraries"))
            return true;
        if (looking_at_no_bounds_check(start, "make_directory"))
            return true;
        if (looking_at_no_bounds_check(start, "subdir_depends"))
            return true;
        if (looking_at_no_bounds_check(start, "target_sources"))
            return true;
        if (looking_at_no_bounds_check(start, "utility_source"))
            return true;
        if (looking_at_no_bounds_check(start, "variable_watch"))
            return true;
        return false;

    case 15:
        if (looking_at_no_bounds_check(start, "add_definitions"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_configure"))
            return true;
        if (looking_at_no_bounds_check(start, "define_property"))
            return true;
        if (looking_at_no_bounds_check(start, "enable_language"))
            return true;
        if (looking_at_no_bounds_check(start, "execute_process"))
            return true;
        if (looking_at_no_bounds_check(start, "install_targets"))
            return true;
        return false;

    case 16:
        if (looking_at_no_bounds_check(start, "add_dependencies"))
            return true;
        if (looking_at_no_bounds_check(start, "add_link_options"))
            return true;
        if (looking_at_no_bounds_check(start, "add_subdirectory"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_run_script"))
            return true;
        if (looking_at_no_bounds_check(start, "install_programs"))
            return true;
        if (looking_at_no_bounds_check(start, "link_directories"))
            return true;
        if (looking_at_no_bounds_check(start, "mark_as_advanced"))
            return true;
        if (looking_at_no_bounds_check(start, "use_mangled_mesa"))
            return true;
        return false;

    case 17:
        if (looking_at_no_bounds_check(start, "add_custom_target"))
            return true;
        if (looking_at_no_bounds_check(start, "get_test_property"))
            return true;
        if (looking_at_no_bounds_check(start, "variable_requires"))
            return true;
        return false;

    case 18:
        if (looking_at_no_bounds_check(start, "add_custom_command"))
            return true;
        if (looking_at_no_bounds_check(start, "get_cmake_property"))
            return true;
        if (looking_at_no_bounds_check(start, "remove_definitions"))
            return true;
        if (looking_at_no_bounds_check(start, "separate_arguments"))
            return true;
        return false;

    case 19:
        if (looking_at_no_bounds_check(start, "add_compile_options"))
            return true;
        if (looking_at_no_bounds_check(start, "get_target_property"))
            return true;
        if (looking_at_no_bounds_check(start, "include_directories"))
            return true;
        if (looking_at_no_bounds_check(start, "target_link_options"))
            return true;
        return false;

    case 20:
        if (looking_at_no_bounds_check(start, "aux_source_directory"))
            return true;
        if (looking_at_no_bounds_check(start, "set_tests_properties"))
            return true;
        return false;

    case 21:
        if (looking_at_no_bounds_check(start, "cmake_parse_arguments"))
            return true;
        if (looking_at_no_bounds_check(start, "output_required_files"))
            return true;
        if (looking_at_no_bounds_check(start, "set_target_properties"))
            return true;
        if (looking_at_no_bounds_check(start, "target_link_libraries"))
            return true;
        return false;

    case 22:
        if (looking_at_no_bounds_check(start, "cmake_minimum_required"))
            return true;
        if (looking_at_no_bounds_check(start, "create_test_sourcelist"))
            return true;
        if (looking_at_no_bounds_check(start, "get_directory_property"))
            return true;
        if (looking_at_no_bounds_check(start, "get_filename_component"))
            return true;
        if (looking_at_no_bounds_check(start, "target_compile_options"))
            return true;
        return false;

    case 23:
        if (looking_at_no_bounds_check(start, "add_compile_definitions"))
            return true;
        if (looking_at_no_bounds_check(start, "ctest_read_custom_files"))
            return true;
        if (looking_at_no_bounds_check(start, "target_compile_features"))
            return true;
        if (looking_at_no_bounds_check(start, "target_link_directories"))
            return true;
        return false;

    case 24:
        if (looking_at_no_bounds_check(start, "get_source_file_property"))
            return true;
        if (looking_at_no_bounds_check(start, "set_directory_properties"))
            return true;
        return false;

    case 25:
        if (looking_at_no_bounds_check(start, "target_precompile_headers"))
            return true;
        return false;

    case 26:
        if (looking_at_no_bounds_check(start, "include_external_msproject"))
            return true;
        if (looking_at_no_bounds_check(start, "include_regular_expression"))
            return true;
        if (looking_at_no_bounds_check(start, "target_compile_definitions"))
            return true;
        if (looking_at_no_bounds_check(start, "target_include_directories"))
            return true;
        return false;

    case 27:
        if (looking_at_no_bounds_check(start, "export_library_dependencies"))
            return true;
        if (looking_at_no_bounds_check(start, "set_source_files_properties"))
            return true;
        return false;

    case 28:
        if (looking_at_no_bounds_check(start, "ctest_empty_binary_directory"))
            return true;
        return false;

    case 29:
        if (looking_at_no_bounds_check(start, "cmake_host_system_information"))
            return true;
        return false;

    default:
        return false;
    }
}

bool cmake_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    ZoneScoped;

    if (!advance_whitespace(iterator)) {
        return false;
    }

    Contents_Iterator start = *iterator;
    token->start = iterator->position;
    char first_ch = iterator->get();
    iterator->advance();

    if (!is_special(first_ch)) {
        while (!iterator->at_eob() && !is_special(iterator->get())) {
            iterator->advance();
        }

        if (looking_at_keyword(start, iterator->position - start.position)) {
            token->type = Token_Type::KEYWORD;
        } else {
            token->type = Token_Type::IDENTIFIER;
        }
        goto ret;
    }

    if (first_ch == '"' || first_ch == '\'') {
        while (!iterator->at_eob()) {
            if (iterator->get() == first_ch) {
                iterator->advance();
                break;
            }
            if (iterator->get() == '\\') {
                iterator->advance();
                if (iterator->at_eob()) {
                    break;
                }
            }
            iterator->advance();
        }

        token->type = Token_Type::STRING;
        goto ret;
    }

    if (first_ch == '$') {
        token->type = Token_Type::PUNCTUATION;
        goto ret;
    }

    if (first_ch == '{' || first_ch == '[' || first_ch == '(') {
        token->type = Token_Type::OPEN_PAIR;
        goto ret;
    }
    if (first_ch == '}' || first_ch == ']' || first_ch == ')') {
        token->type = Token_Type::CLOSE_PAIR;
        goto ret;
    }

    if (first_ch == '#') {
        end_of_line(iterator);
        token->type = Token_Type::COMMENT;
        goto ret;
    }

    token->type = Token_Type::DEFAULT;

ret:
    token->end = iterator->position;
    return true;
}

}
}
