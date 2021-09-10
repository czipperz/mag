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
    Contents_Iterator test = start;

    switch (length) {
    case 2:
        return looking_at_no_bounds_check(start, "if");

    case 3:
        return looking_at_no_bounds_check(start, "set");

    case 4:
        switch (start.get()) {
        case 'e':
            return looking_at_no_bounds_check(start, "else");
        case 'f':
            return looking_at_no_bounds_check(start, "file");
        case 'l':
            return looking_at_no_bounds_check(start, "list");
        case 'm':
            return looking_at_no_bounds_check(start, "math");
        default:
            return false;
        }

    case 5:
        switch (start.get()) {
        case 'b':
            return looking_at_no_bounds_check(start, "break");
        case 'e':
            return looking_at_no_bounds_check(start, "endif");
        case 'm':
            return looking_at_no_bounds_check(start, "macro");
        case 'u':
            return looking_at_no_bounds_check(start, "unset");
        case 'w':
            return looking_at_no_bounds_check(start, "while");
        default:
            return false;
        }

    case 6:
        switch (start.get()) {
        case 'e':
            test.advance(1);
            switch (test.get()) {
            case 'l':
                return looking_at_no_bounds_check(start, "elseif");
            case 'x':
                return looking_at_no_bounds_check(start, "export");
            default:
                return false;
            }
        case 'o':
            return looking_at_no_bounds_check(start, "option");
        case 'r':
            test.advance(2);
            switch (test.get()) {
            case 'm':
                return looking_at_no_bounds_check(start, "remove");
            case 't':
                return looking_at_no_bounds_check(start, "return");
            default:
                return false;
            }
        case 's':
            return looking_at_no_bounds_check(start, "string");
        default:
            return false;
        }

    case 7:
        test.advance(3);
        switch (test.get()) {
        case 'e':
            return looking_at_no_bounds_check(start, "foreach");
        case 'l':
            return looking_at_no_bounds_check(start, "include");
        case 't':
            return looking_at_no_bounds_check(start, "install");
        case 's':
            return looking_at_no_bounds_check(start, "message");
        case 'j':
            return looking_at_no_bounds_check(start, "project");
        case 'd':
            return looking_at_no_bounds_check(start, "subdirs");
        case '_':
            return looking_at_no_bounds_check(start, "try_run");
        default:
            return false;
        }

    case 8:
        test.advance(3);
        switch (test.get()) {
        case '_':
            return looking_at_no_bounds_check(start, "add_test");
        case 't':
            return looking_at_no_bounds_check(start, "continue");
        case 'm':
            return looking_at_no_bounds_check(start, "endmacro");
        case 'w':
            return looking_at_no_bounds_check(start, "endwhile");
        case 'c':
            return looking_at_no_bounds_check(start, "function");
        default:
            return false;
        }

    case 9:
        test.advance(3);
        switch (test.get()) {
        case 'f':
            return looking_at_no_bounds_check(start, "find_file");
        case 'p':
            return looking_at_no_bounds_check(start, "find_path");
        case 'n':
            return looking_at_no_bounds_check(start, "site_name");
        default:
            return false;
        }

    case 10:
        test.advance(3);
        switch (test.get()) {
        case 'l':
            return looking_at_no_bounds_check(start, "build_name");
        case 'k':
            return looking_at_no_bounds_check(start, "cmake_path");
        case 's':
            return looking_at_no_bounds_check(start, "ctest_test");
        case 'f':
            return looking_at_no_bounds_check(start, "endforeach");
        case 'd':
            return looking_at_no_bounds_check(start, "load_cache");
        case 'w':
            return looking_at_no_bounds_check(start, "qt_wrap_ui");
        case 't':
            return looking_at_no_bounds_check(start, "write_file");
        default:
            return false;
        }

    case 11:
        switch (start.get()) {
        case 'a':
            return looking_at_no_bounds_check(start, "add_library");
        case 'c':
            test.advance(7);
            switch (test.get()) {
            case 'u':
                return looking_at_no_bounds_check(start, "ctest_build");
            case 'l':
                return looking_at_no_bounds_check(start, "ctest_sleep");
            case 't':
                return looking_at_no_bounds_check(start, "ctest_start");
            default:
                return false;
            }
        case 'e':
            return looking_at_no_bounds_check(start, "endfunction");
        case 'q':
            return looking_at_no_bounds_check(start, "qt_wrap_cpp");
        case 't':
            return looking_at_no_bounds_check(start, "try_compile");
        default:
            return false;
        }

    case 12:
        switch (test.get()) {
        case 'c':
            test.advance(9);
            switch (test.get()) {
            case 'i':
                return looking_at_no_bounds_check(start, "cmake_policy");
            case 'm':
                return looking_at_no_bounds_check(start, "ctest_submit");
            case 'a':
                return looking_at_no_bounds_check(start, "ctest_update");
            case 'o':
                return looking_at_no_bounds_check(start, "ctest_upload");
            default:
                return false;
            }
        case 'e':
            return looking_at_no_bounds_check(start, "exec_program");
        case 'f':
            test.advance(7);
            switch (test.get()) {
            case 'b':
                return looking_at_no_bounds_check(start, "find_library");
            case 'c':
                return looking_at_no_bounds_check(start, "find_package");
            case 'o':
                return looking_at_no_bounds_check(start, "find_program");
            case 'a':
                return looking_at_no_bounds_check(start, "fltk_wrap_ui");
            default:
                return false;
            }
        case 'g':
            return looking_at_no_bounds_check(start, "get_property");
        case 'l':
            return looking_at_no_bounds_check(start, "load_command");
        case 's':
            test.advance(1);
            switch (test.get()) {
            case 'e':
                return looking_at_no_bounds_check(start, "set_property");
            case 'o':
                return looking_at_no_bounds_check(start, "source_group");
            default:
                return false;
            }
        default:
            return false;
        }

    case 13:
        test.advance(3);
        switch (test.get()) {
        case 'i':
            return looking_at_no_bounds_check(start, "build_command");
        case 'c':
            return looking_at_no_bounds_check(start, "include_guard");
        case 's':
            return looking_at_no_bounds_check(start, "install_files");
        default:
            return false;
        }

    case 14:
        switch (start.get()) {
        case 'a':
            return looking_at_no_bounds_check(start, "add_executable");
        case 'c':
            test.advance(6);
            switch (test.get()) {
            case 'l':
                return looking_at_no_bounds_check(start, "cmake_language");
            case 'u':
                return looking_at_no_bounds_check(start, "configure_file");
            case 'c':
                return looking_at_no_bounds_check(start, "ctest_coverage");
            case 'm':
                return looking_at_no_bounds_check(start, "ctest_memcheck");
            default:
                return false;
            }
        case 'e':
            return looking_at_no_bounds_check(start, "enable_testing");
        case 'l':
            return looking_at_no_bounds_check(start, "link_libraries");
        case 'm':
            return looking_at_no_bounds_check(start, "make_directory");
        case 's':
            return looking_at_no_bounds_check(start, "subdir_depends");
        case 't':
            return looking_at_no_bounds_check(start, "target_sources");
        case 'u':
            return looking_at_no_bounds_check(start, "utility_source");
        case 'v':
            return looking_at_no_bounds_check(start, "variable_watch");
        default:
            return false;
        }

    case 15:
        switch (start.get()) {
        case 'a':
            return looking_at_no_bounds_check(start, "add_definitions");
        case 'c':
            return looking_at_no_bounds_check(start, "ctest_configure");
        case 'd':
            return looking_at_no_bounds_check(start, "define_property");
        case 'e':
            test.advance(1);
            switch (test.get()) {
            case 'n':
                return looking_at_no_bounds_check(start, "enable_language");
            case 'x':
                return looking_at_no_bounds_check(start, "execute_process");
            default:
                return false;
            }
        case 'i':
            return looking_at_no_bounds_check(start, "install_targets");
        default:
            return false;
        }

    case 16:
        switch (start.get()) {
        case 'a':
            test.advance(4);
            switch (test.get()) {
            case 'd':
                return looking_at_no_bounds_check(start, "add_dependencies");
            case 'l':
                return looking_at_no_bounds_check(start, "add_link_options");
            case 's':
                return looking_at_no_bounds_check(start, "add_subdirectory");
            default:
                return false;
            }
        case 'c':
            return looking_at_no_bounds_check(start, "ctest_run_script");
        case 'i':
            return looking_at_no_bounds_check(start, "install_programs");
        case 'l':
            return looking_at_no_bounds_check(start, "link_directories");
        case 'm':
            return looking_at_no_bounds_check(start, "mark_as_advanced");
        case 'u':
            return looking_at_no_bounds_check(start, "use_mangled_mesa");
        default:
            return false;
        }

    case 17:
        switch (start.get()) {
        case 'a':
            return looking_at_no_bounds_check(start, "add_custom_target");
        case 'g':
            return looking_at_no_bounds_check(start, "get_test_property");
        case 'v':
            return looking_at_no_bounds_check(start, "variable_requires");
        default:
            return false;
        }

    case 18:
        switch (start.get()) {
        case 'a':
            return looking_at_no_bounds_check(start, "add_custom_command");
        case 'g':
            return looking_at_no_bounds_check(start, "get_cmake_property");
        case 'r':
            return looking_at_no_bounds_check(start, "remove_definitions");
        case 's':
            return looking_at_no_bounds_check(start, "separate_arguments");
        default:
            return false;
        }

    case 19:
        switch (start.get()) {
        case 'a':
            return looking_at_no_bounds_check(start, "add_compile_options");
        case 'g':
            return looking_at_no_bounds_check(start, "get_target_property");
        case 'i':
            return looking_at_no_bounds_check(start, "include_directories");
        case 't':
            return looking_at_no_bounds_check(start, "target_link_options");
        default:
            return false;
        }

    case 20:
        switch (start.get()) {
        case 'a':
            return looking_at_no_bounds_check(start, "aux_source_directory");
        case 's':
            return looking_at_no_bounds_check(start, "set_tests_properties");
        default:
            return false;
        }

    case 21:
        switch (start.get()) {
        case 'c':
            return looking_at_no_bounds_check(start, "cmake_parse_arguments");
        case 'o':
            return looking_at_no_bounds_check(start, "output_required_files");
        case 's':
            return looking_at_no_bounds_check(start, "set_target_properties");
        case 't':
            return looking_at_no_bounds_check(start, "target_link_libraries");
        default:
            return false;
        }

    case 22:
        test.advance(7);
        switch (test.get()) {
        case 'i':
            return looking_at_no_bounds_check(start, "cmake_minimum_required");
        case 's':
            return looking_at_no_bounds_check(start, "create_test_sourcelist");
        case 't':
            return looking_at_no_bounds_check(start, "get_directory_property");
        case 'a':
            return looking_at_no_bounds_check(start, "get_filename_component");
        case 'm':
            return looking_at_no_bounds_check(start, "target_compile_options");
        default:
            return false;
        }

    case 23:
        test.advance(7);
        switch (test.get()) {
        case 'p':
            return looking_at_no_bounds_check(start, "add_compile_definitions");
        case 'e':
            return looking_at_no_bounds_check(start, "ctest_read_custom_files");
        case 'c':
            return looking_at_no_bounds_check(start, "target_compile_features");
        case 'l':
            return looking_at_no_bounds_check(start, "target_link_directories");
        default:
            return false;
        }

    case 24:
        switch (start.get()) {
        case 'g':
            return looking_at_no_bounds_check(start, "get_source_file_property");
        case 's':
            return looking_at_no_bounds_check(start, "set_directory_properties");
        default:
            return false;
        }

    case 25:
        return looking_at_no_bounds_check(start, "target_precompile_headers");

    case 26:
        test.advance(8);
        switch (test.get()) {
        case 'e':
            return looking_at_no_bounds_check(start, "include_external_msproject");
        case 'r':
            return looking_at_no_bounds_check(start, "include_regular_expression");
        case 'o':
            return looking_at_no_bounds_check(start, "target_compile_definitions");
        case 'n':
            return looking_at_no_bounds_check(start, "target_include_directories");
        default:
            return false;
        }

    case 27:
        switch (start.get()) {
        case 'e':
            return looking_at_no_bounds_check(start, "export_library_dependencies");
        case 's':
            return looking_at_no_bounds_check(start, "set_source_files_properties");
        default:
            return false;
        }

    case 28:
        return looking_at_no_bounds_check(start, "ctest_empty_binary_directory");

    case 29:
        return looking_at_no_bounds_check(start, "cmake_host_system_information");

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
