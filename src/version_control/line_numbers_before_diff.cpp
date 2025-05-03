#include "line_numbers_before_diff.hpp"

#include <cz/format.hpp>
#include <cz/parse.hpp>
#include <cz/process.hpp>
#include <cz/slice.hpp>
#include <cz/str.hpp>

namespace mag {
namespace version_control {

bool line_numbers_before_changes_to_path(const char* working_directory,
                                         cz::Str path,
                                         cz::Slice<uint64_t> line_numbers) {
    cz::String diff_output = {};
    CZ_DEFER(diff_output.drop(cz::heap_allocator()));

    {
        cz::Heap_String head_path = cz::format("HEAD:./", path);
        CZ_DEFER(head_path.drop());
        cz::Str args[] = {"git", "diff", "-U0", head_path, path};

        cz::Input_File std_out;
        CZ_DEFER(std_out.close());
        cz::Process process;
        {
            cz::Process_Options options;
            options.working_directory = working_directory;
            if (!create_process_output_pipe(&options.std_out, &std_out)) {
                return false;
            }
            CZ_DEFER(options.std_out.close());
            if (!process.launch_program(args, options)) {
                return false;
            }
        }

        if (!cz::read_to_string(std_out, cz::heap_allocator(), &diff_output)) {
            return false;
        }

        if (process.join() != 0) {
            return false;
        }
    }

    return line_numbers_before_diff(diff_output, line_numbers);
}

bool line_numbers_before_diff(cz::Str diff_output, cz::Slice<uint64_t> line_numbers) {
    if (line_numbers.len == 0)
        return true;

    size_t line_index = 0;
    for (size_t diff_index = 0; diff_index < diff_output.len;) {
        cz::Str line = diff_output.slice_start(diff_index);
        line = line.slice_end(line.find_index('\n'));
        diff_index += line.len + 1;
        if (!line.starts_with("@@ "))
            continue;  // We only really care about the line number headers.

        uint64_t before_line, before_len = 1, after_line, after_len = 1;
        if (cz::parse_advance(&line, "@@ -", &before_line) <= 0)
            return false;
        if (cz::parse_advance(&line, ',', &before_len) < 0)  // optional
            return false;
        if (cz::parse_advance(&line, " +", &after_line) <= 0)
            return false;
        if (cz::parse_advance(&line, ',', &after_len) < 0)  // optional
            return false;
        if (cz::parse_advance(&line, " @@") <= 0)
            return false;

        while (line_numbers[line_index] < before_line) {
            ++line_index;
            if (line_index == line_numbers.len) {
                return true;
            }
        }
        int64_t offset = before_len - after_len;
        while (line_numbers[line_index] < before_line + after_len) {
            line_numbers[line_index] = before_line;
            ++line_index;
            if (line_index == line_numbers.len) {
                return true;
            }
        }
        for (size_t l = line_index; l < line_numbers.len; ++l) {
            line_numbers[l] += offset;
        }
    }
    return true;
}

}
}
