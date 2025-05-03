#include <czt/test_base.hpp>

#include "version_control/line_numbers_before_diff.hpp"

using namespace mag::version_control;

const char* diff_output = R"(
diff --git src/custom/config.cpp src/custom/config.cpp
index 27a0d2b..2ec6cc4 100644
--- src/custom/config.cpp
+++ src/custom/config.cpp
@@ -485,5 +484,0 @@ static void create_key_map(Key_Map& key_map) {
-    BIND(key_map, "A-g A-g", command_goto_line);
-    BIND(key_map, "A-g G", command_goto_position);
-    BIND(key_map, "A-g g", command_show_file_length_info);
-    BIND(key_map, "A-g c", command_goto_column);
-
@@ -507,3 +502,3 @@ static void create_key_map(Key_Map& key_map) {
-    BIND(key_map, "A-g f", prose::command_find_file_in_version_control);
-    BIND(key_map, "A-g h", prose::command_find_file_in_current_directory);
-    BIND(key_map, "A-g d", prose::command_find_file_diff_master);
+    BIND(key_map, "A-c f", prose::command_find_file_in_version_control);
+    BIND(key_map, "A-c h", prose::command_find_file_in_current_directory);
+    BIND(key_map, "A-c d", prose::command_find_file_diff_master);
@@ -517,0 +513,2 @@ static void create_key_map(Key_Map& key_map) {
+    BIND(key_map, "MOUSE5", command_do_nothing);
+
@@ -592 +589 @@ static void create_theme(Theme& theme) {
-    theme.token_faces[Token_Type::PREPROCESSOR_ELSE] = {219, {}, 0};
+    theme.token_faces[Token_Type::PREPROCESSOR_ELSE] = {219, {}, 1};
)";

TEST_CASE("version_control:::line_numbers_before_diff: clear assertions") {
    uint64_t line_numbers[] = {400, 500, 600};
    CHECK(line_numbers_before_diff(diff_output, cz::slice(line_numbers)));
    CHECK(line_numbers[0] == 400);
    CHECK(line_numbers[1] == 505);
    CHECK(line_numbers[2] == 603);
}

TEST_CASE("version_control:::line_numbers_before_diff: around removal") {
    uint64_t line_numbers[] = {484, 485};
    CHECK(line_numbers_before_diff(diff_output, cz::slice(line_numbers)));
    CHECK(line_numbers[0] == 484);
    CHECK(line_numbers[1] == 490);
}

TEST_CASE("version_control:::line_numbers_before_diff: around change") {
    uint64_t line_numbers[] = {501, 502, 503, 504, 505, 506};
    CHECK(line_numbers_before_diff(diff_output, cz::slice(line_numbers)));
    CHECK(line_numbers[0] == 506);
    CHECK(line_numbers[1] == 507);
    CHECK(line_numbers[2] == 507);
    CHECK(line_numbers[3] == 507);
    CHECK(line_numbers[4] == 510);
    CHECK(line_numbers[5] == 511);
}

TEST_CASE("version_control:::line_numbers_before_diff: around insertion") {
    uint64_t line_numbers[] = {511, 512, 513, 514, 515};
    CHECK(line_numbers_before_diff(diff_output, cz::slice(line_numbers)));
    CHECK(line_numbers[0] == 516);
    CHECK(line_numbers[1] == 517);
    CHECK(line_numbers[2] == 517);
    CHECK(line_numbers[3] == 517);
    CHECK(line_numbers[4] == 518);
}

TEST_CASE("version_control:::line_numbers_before_diff: around single line change") {
    uint64_t line_numbers[] = {589, 590};
    CHECK(line_numbers_before_diff(diff_output, cz::slice(line_numbers)));
    CHECK(line_numbers[0] == 592);
    CHECK(line_numbers[1] == 593);
}

// To ensure no segfault due to read out of bounds.
TEST_CASE("version_control:::line_numbers_before_diff: no line numbers") {
    CHECK(line_numbers_before_diff(diff_output, cz::Slice<uint64_t>(nullptr, 0)));
}
