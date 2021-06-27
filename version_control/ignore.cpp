#include "ignore.hpp"

#include <Tracy.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>

namespace mag {
namespace version_control {

static void process_line(cz::Str line, Ignore_Rules* rules) {
    ZoneScoped;

    // Ignore empty lines.
    if (line.len == 0) {
        return;
    }

    // Ignore comments.
    if (line[0] == '#') {
        return;
    }

    // If the line starts with * then we assume it is a rule like `*.txt` for now.
    if (line[0] == '*') {
        line = line.slice_start(1);

        // TODO: what if the new `line` has advanced characters?
        cz::String string = {};
        string.reserve(cz::heap_allocator(), line.len);
        string.append(line);

        rules->suffix_rules.reserve(cz::heap_allocator(), 1);
        rules->suffix_rules.push(string);
        return;
    }

    // Line starting with /.
    if (line[0] == '/') {
        // TODO: what if the rest of `line` has advanced characters?
        cz::String string = {};
        string.reserve(cz::heap_allocator(), line.len);
        string.append(line);

        rules->exact_rules.reserve(cz::heap_allocator(), 1);
        rules->exact_rules.push(string);
        return;
    }

    // Default: match /line.
    cz::String string = {};
    string.reserve(cz::heap_allocator(), line.len + 1);
    string.push('/');
    string.append(line);

    rules->suffix_rules.reserve(cz::heap_allocator(), 1);
    rules->suffix_rules.push(string);
    return;
}

void parse_ignore_rules(cz::Str contents, Ignore_Rules* rules) {
    ZoneScoped;

    size_t index = 0;
    while (1) {
        size_t end;
        if (const char* endp = contents.slice_start(index).find('\n')) {
            end = endp - contents.start();
        } else {
            end = contents.len;
        }

        process_line(contents.slice(index, end), rules);

        index = end + 1;
        if (index >= contents.len) {
            break;
        }
        continue;
    }
}

static void try_ignore_git_modules(const char* path, Ignore_Rules* rules) {
    cz::Input_File file;
    if (!file.open(path)) {
        return;
    }
    CZ_DEFER(file.close());

    cz::String contents = {};
    CZ_DEFER(contents.drop(cz::heap_allocator()));
    read_to_string(file, cz::heap_allocator(), &contents);

    const char* start = contents.start();
    while (1) {
        // TODO: actually parse this as ini file or whatever.
        const char* sub = contents.slice_start(start).find("path = ");
        if (!sub) {
            break;
        }

        const char* eol = contents.slice_start(sub).find('\n');
        if (!eol) {
            eol = contents.end();
        }

        cz::Str path = contents.slice(sub + 7, eol);
        cz::String string = {};
        string.reserve(cz::heap_allocator(), 1 + path.len);
        string.push('/');
        string.append(path);

        rules->exact_rules.reserve(cz::heap_allocator(), 1);
        rules->exact_rules.push(string);

        if (eol == contents.end()) {
            break;
        }
        start = eol;
    }
}

void find_ignore_rules(cz::Str root, Ignore_Rules* rules) {
    ZoneScoped;

    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));

    path.reserve(cz::heap_allocator(), root.len + 1 + 11 + 1);
    path.append(root);
    path.push('/');

    cz::String contents = {};
    CZ_DEFER(contents.drop(cz::heap_allocator()));

    cz::Input_File file;

    size_t initial_len = path.len();
    path.append(".ignore");
    path.null_terminate();
    if (file.open(path.buffer())) {
        CZ_DEFER(file.close());

        read_to_string(file, cz::heap_allocator(), &contents);
        parse_ignore_rules(contents, rules);
    }

    path.set_len(initial_len);
    path.append(".gitignore");
    path.null_terminate();
    if (file.open(path.buffer())) {
        CZ_DEFER(file.close());

        // Add a special line such that .git is ignored.
        process_line("/.git", rules);

        // Don't find files in Git submodules.
        path.set_len(initial_len);
        path.append(".gitmodules");
        path.null_terminate();
        try_ignore_git_modules(path.buffer(), rules);

        contents.set_len(0);
        read_to_string(file, cz::heap_allocator(), &contents);
        parse_ignore_rules(contents, rules);
    }

    // TODO: find and parse SVN ignore files
}

bool file_matches(const Ignore_Rules& rules, cz::Str path) {
    ZoneScoped;

    // Test the suffix rules.
    for (size_t i = 0; i < rules.suffix_rules.len(); ++i) {
        if (path.ends_with(rules.suffix_rules[i])) {
            return true;
        }
    }

    // Test the exact rules.
    for (size_t i = 0; i < rules.exact_rules.len(); ++i) {
        if (path == rules.exact_rules[i]) {
            return true;
        }
    }

    // No rules match.
    return false;
}

void Ignore_Rules::drop() {
    auto& rules = *this;

    for (size_t i = 0; i < rules.suffix_rules.len(); ++i) {
        rules.suffix_rules[i].drop(cz::heap_allocator());
    }
    rules.suffix_rules.drop(cz::heap_allocator());

    for (size_t i = 0; i < rules.exact_rules.len(); ++i) {
        rules.exact_rules[i].drop(cz::heap_allocator());
    }
    rules.exact_rules.drop(cz::heap_allocator());
}

}
}
