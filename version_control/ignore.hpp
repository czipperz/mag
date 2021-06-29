#pragma once

#include <cz/string.hpp>
#include <cz/vector.hpp>

namespace mag {
namespace version_control {

struct Rule {
    cz::String string;
    size_t index;
    bool inverse;
};

struct Ignore_Rules {
    cz::Vector<Rule> suffix_rules;
    cz::Vector<Rule> exact_rules;

    void drop();
};

/// Find ignore rules based of the ignore files present in the `root` directory.
void find_ignore_rules(cz::Str root, Ignore_Rules* rules);

/// Parse ignore rules from a ignore file's contents.
void parse_ignore_rules(cz::Str contents, Ignore_Rules* rules);

/// Test if `path` matches any rules.
bool file_matches(const Ignore_Rules& rules, cz::Str path);

}
}
