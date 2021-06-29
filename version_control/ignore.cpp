#include "ignore.hpp"

#include <Tracy.hpp>
#include <cz/defer.hpp>
#include <cz/heap.hpp>
#include <cz/process.hpp>

namespace mag {
namespace version_control {

static void process_line(cz::Str line, Ignore_Rules* rules, size_t* counter) {
    ZoneScoped;

    // Ignore empty lines.
    if (line.len == 0) {
        return;
    }

    // Ignore comments.
    if (line[0] == '#') {
        return;
    }

    Rule rule = {};
    rule.index = (*counter)++;

    // Negative patterns start with !.
    if (line[0] == '!') {
        line = line.slice_start(1);
        rule.inverse = true;
    }

    // If the line starts with a backslash then the next character is
    // treated literally.  This isn't really exactly correct but it's close.
    if (line[0] == '\\') {
        line = line.slice_start(1);
    }

    // If the line starts with * then we assume it is a rule like `*.txt` for now.
    if (line[0] == '*') {
        line = line.slice_start(1);

        // TODO: what if the new `line` has advanced characters?
        rule.string.reserve(cz::heap_allocator(), line.len);
        rule.string.append(line);

        rules->suffix_rules.reserve(cz::heap_allocator(), 1);
        rules->suffix_rules.push(rule);
        return;
    }

    // Line ending in * is prefix.
    if (line[0] == '/' && line.ends_with('*')) {
        // TODO: what if the rest of `line` has advanced characters?
        rule.string = line.slice_end(line.len - 1).clone(cz::heap_allocator());

        rules->exact_rules.reserve(cz::heap_allocator(), 1);
        rules->exact_rules.push(rule);
        return;
    }

    // Line with / at start is an exact match.
    if (line[0] == '/') {
        // TODO: what if the rest of `line` has advanced characters?
        rule.string.reserve(cz::heap_allocator(), line.len);
        rule.string.append(line);

        rules->exact_rules.reserve(cz::heap_allocator(), 1);
        rules->exact_rules.push(rule);
        return;
    }

    // Default: match /line.
    rule.string.reserve(cz::heap_allocator(), line.len + 1);
    rule.string.push('/');
    rule.string.append(line);

    rules->suffix_rules.reserve(cz::heap_allocator(), 1);
    rules->suffix_rules.push(rule);
    return;
}

static void parse_ignore_rules(cz::Str contents, Ignore_Rules* rules, size_t* counter) {
    ZoneScoped;

    size_t index = 0;
    while (1) {
        size_t end;
        if (const char* endp = contents.slice_start(index).find('\n')) {
            end = endp - contents.start();
        } else {
            end = contents.len;
        }

        process_line(contents.slice(index, end), rules, counter);

        index = end + 1;
        if (index >= contents.len) {
            break;
        }
        continue;
    }
}

void parse_ignore_rules(cz::Str contents, Ignore_Rules* rules) {
    size_t counter = 0;
    return parse_ignore_rules(contents, rules, &counter);
}

static void try_ignore_git_modules(const char* path, Ignore_Rules* rules, size_t* counter) {
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

        Rule rule = {};
        rule.index = (*counter)++;

        rule.string.reserve(cz::heap_allocator(), 1 + path.len);
        rule.string.push('/');
        rule.string.append(path);

        rules->exact_rules.reserve(cz::heap_allocator(), 1);
        rules->exact_rules.push(rule);

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

    size_t counter = 0;

    size_t initial_len = path.len();
    path.append(".ignore");
    path.null_terminate();
    if (file.open(path.buffer())) {
        CZ_DEFER(file.close());

        read_to_string(file, cz::heap_allocator(), &contents);
        parse_ignore_rules(contents, rules, &counter);
    }

    path.set_len(initial_len);
    path.append(".gitignore");
    path.null_terminate();
    if (file.open(path.buffer())) {
        CZ_DEFER(file.close());

        // Add a special line such that .git is ignored.
        process_line("/.git", rules, &counter);

        // Don't find files in Git submodules.
        path.set_len(initial_len);
        path.append(".gitmodules");
        path.null_terminate();
        try_ignore_git_modules(path.buffer(), rules, &counter);

        contents.set_len(0);
        read_to_string(file, cz::heap_allocator(), &contents);
        parse_ignore_rules(contents, rules, &counter);
    }

    // TODO: find and parse SVN ignore files
}

bool file_matches(const Ignore_Rules& rules, cz::Str path) {
    ZoneScoped;

    Rule rule;
    rule.index = 0;
    rule.inverse = true;

    // Test the suffix rules.
    for (size_t i = rules.suffix_rules.len(); i-- > 0;) {
        if (path.ends_with(rules.suffix_rules[i].string)) {
            rule = rules.suffix_rules[i];
            break;
        }
    }

    // Test the exact rules.
    for (size_t i = rules.exact_rules.len(); i-- > 0;) {
        if (rules.exact_rules[i].index < rule.index) {
            break;
        }

        if (path.starts_with(rules.exact_rules[i].string)) {
            if (path.len == rules.exact_rules[i].string.len() ||
                rules.exact_rules[i].string[rules.exact_rules[i].string.len() - 1] == '/' ||
                path[rules.exact_rules[i].string.len()] == '/') {
                rule = rules.exact_rules[i];
                break;
            }
        }
    }

    return !rule.inverse;
}

void Ignore_Rules::drop() {
    auto& rules = *this;

    for (size_t i = 0; i < rules.suffix_rules.len(); ++i) {
        rules.suffix_rules[i].string.drop(cz::heap_allocator());
    }
    rules.suffix_rules.drop(cz::heap_allocator());

    for (size_t i = 0; i < rules.exact_rules.len(); ++i) {
        rules.exact_rules[i].string.drop(cz::heap_allocator());
    }
    rules.exact_rules.drop(cz::heap_allocator());
}

}
}
