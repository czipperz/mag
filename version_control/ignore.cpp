#include "ignore.hpp"

#include <Tracy.hpp>
#include <cz/defer.hpp>
#include <cz/env.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>

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
        if (line.len == 0)
            return; // Ignore invalid lines.
    }


    // If the line starts with a backslash then the next character is
    // treated literally.  This isn't really exactly correct but it's close.
    if (line[0] == '\\') {
        line = line.slice_start(1);
        if (line.len == 0)
            return; // Ignore invalid lines.
    }

    // Ignore trailing '/'.  This is supposed to detect directories, but it is
    // slow to do that and in practice nobody cares if it picks up files too.
    if (line.last() == '/') {
        --line.len;
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

    cz::Str remaining = contents;
    while (1) {
        cz::Str line = remaining;
        bool split = remaining.split_excluding('\n', &line, &remaining);

        process_line(line, rules, counter);

        if (!split) {
            break;
        }
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
    cz::read_to_string(file, cz::heap_allocator(), &contents);

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

static bool parse_ignore_file(cz::String* path,
                              cz::Str name,
                              cz::String* contents,
                              Ignore_Rules* rules,
                              size_t* counter) {
    size_t initial_len = path->len;
    path->reserve(cz::heap_allocator(), name.len + 1);
    path->append(name);
    path->null_terminate();

    cz::Input_File file;
    bool opened = file.open(path->buffer);
    path->len = initial_len;

    if (opened) {
        CZ_DEFER(file.close());
        read_to_string(file, cz::heap_allocator(), contents);
        parse_ignore_rules(*contents, rules, counter);
        contents->len = 0;
    }
    return opened;
}

void find_ignore_rules(cz::Str root, Ignore_Rules* rules) {
    ZoneScoped;

    size_t counter = 0;

    // Always ignore .git and .svn directories.
    process_line(".git", rules, &counter);
    process_line(".svn", rules, &counter);

    // Set path as project root.
    cz::String path = {};
    CZ_DEFER(path.drop(cz::heap_allocator()));
    path.reserve(cz::heap_allocator(), root.len + 1 + 11 + 1);
    path.append(root);
    path.push('/');

    // Reuse the buffer for file contents.
    cz::String contents = {};
    CZ_DEFER(contents.drop(cz::heap_allocator()));

    // Parse ignore files in project root.
    parse_ignore_file(&path, ".ignore", &contents, rules, &counter);
    parse_ignore_file(&path, ".agignore", &contents, rules, &counter);
    parse_ignore_file(&path, ".hgignore", &contents, rules, &counter);
    bool git = parse_ignore_file(&path, ".gitignore", &contents, rules, &counter);

    // Ignore Git submodules.
    if (git) {
        size_t initial_len = path.len;
        path.append(".gitmodules");
        path.null_terminate();
        try_ignore_git_modules(path.buffer, rules, &counter);
        path.len = initial_len;
    }

    // TODO: find and parse SVN ignore files

    // Global configuration in home directory.
    path.len = 0;
    if (cz::env::get_home(cz::heap_allocator(), &path)) {
        parse_ignore_file(&path, "/.ignore", &contents, rules, &counter);
        parse_ignore_file(&path, "/.agignore", &contents, rules, &counter);
        if (git) {
            parse_ignore_file(&path, "/.gitignore", &contents, rules, &counter);
        }
    }
}

bool file_matches(const Ignore_Rules& rules, cz::Str path) {
    ZoneScoped;

    Rule rule;
    rule.index = 0;
    rule.inverse = true;

    // Test the suffix rules.
    for (size_t i = rules.suffix_rules.len; i-- > 0;) {
        if (path.ends_with(rules.suffix_rules[i].string)) {
            rule = rules.suffix_rules[i];
            break;
        }
    }

    // Test the exact rules.
    for (size_t i = rules.exact_rules.len; i-- > 0;) {
        if (rules.exact_rules[i].index < rule.index) {
            break;
        }

        if (path.starts_with(rules.exact_rules[i].string)) {
            if (path.len == rules.exact_rules[i].string.len ||
                rules.exact_rules[i].string[rules.exact_rules[i].string.len - 1] == '/' ||
                path[rules.exact_rules[i].string.len] == '/') {
                rule = rules.exact_rules[i];
                break;
            }
        }
    }

    return !rule.inverse;
}

void Ignore_Rules::drop() {
    auto& rules = *this;

    for (size_t i = 0; i < rules.suffix_rules.len; ++i) {
        rules.suffix_rules[i].string.drop(cz::heap_allocator());
    }
    rules.suffix_rules.drop(cz::heap_allocator());

    for (size_t i = 0; i < rules.exact_rules.len; ++i) {
        rules.exact_rules[i].string.drop(cz::heap_allocator());
    }
    rules.exact_rules.drop(cz::heap_allocator());
}

}
}
