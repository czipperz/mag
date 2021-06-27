#include <czt/test_base.hpp>

#include "version_control/ignore.hpp"

using namespace mag::version_control;

TEST_CASE("version_control_ignore: no rules") {
    Ignore_Rules rules = {};
    CZ_DEFER(rules.drop());
    parse_ignore_rules("", &rules);

    CHECK_FALSE(file_matches(rules, "/src"));
    CHECK_FALSE(file_matches(rules, "/test/123"));
}

TEST_CASE("version_control_ignore: empty lines and comments") {
    Ignore_Rules rules = {};
    CZ_DEFER(rules.drop());
    parse_ignore_rules("#src\n\n\n#test", &rules);

    CHECK_FALSE(file_matches(rules, "/src"));
    CHECK_FALSE(file_matches(rules, "/test/123"));
}

TEST_CASE("version_control_ignore: basic rule") {
    Ignore_Rules rules = {};
    CZ_DEFER(rules.drop());
    parse_ignore_rules("src", &rules);

    CHECK(file_matches(rules, "/src"));
    CHECK(file_matches(rules, "/abc/src"));
    CHECK_FALSE(file_matches(rules, "/src1"));
    CHECK_FALSE(file_matches(rules, "/asrc"));
    CHECK_FALSE(file_matches(rules, "/test/123"));
}

TEST_CASE("version_control_ignore: *.txt") {
    Ignore_Rules rules = {};
    CZ_DEFER(rules.drop());
    parse_ignore_rules("*.txt", &rules);

    CHECK_FALSE(file_matches(rules, "/src"));
    CHECK(file_matches(rules, "/test.txt"));
    CHECK(file_matches(rules, "/src/test.txt"));
    CHECK(file_matches(rules, "/src/.txt"));
    CHECK_FALSE(file_matches(rules, "/src/.txt2"));
    CHECK_FALSE(file_matches(rules, "/src/.txt/ooga"));
}

TEST_CASE("version_control_ignore: /test.txt") {
    Ignore_Rules rules = {};
    CZ_DEFER(rules.drop());
    parse_ignore_rules("/test.txt", &rules);

    CHECK_FALSE(file_matches(rules, "/src"));
    CHECK(file_matches(rules, "/test.txt"));
    CHECK_FALSE(file_matches(rules, "/test.txt2"));
    CHECK_FALSE(file_matches(rules, "/src/test.txt"));
}
