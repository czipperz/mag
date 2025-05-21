#include <czt/test_base.hpp>

#include "core/completion.hpp"
#include "test_runner.hpp"

using namespace mag;

TEST_CASE("spaces_are_wildcards_completion_filter") {
    Completion_Filter_Context context = {};
    Completion_Engine_Context engine_context = {};
    engine_context.init();
    CZ_DEFER(context.drop());
    CZ_DEFER(engine_context.drop());
    engine_context.results.reserve(11);
    engine_context.results.push("src/prose/");
    engine_context.results.push("src/prose/alternate.cpp");
    engine_context.results.push("src/prose/alternate.hpp");
    engine_context.results.push("src/prose/find_file.cpp");
    engine_context.results.push("src/prose/find_file.hpp");
    engine_context.results.push("src/prose/helpers.cpp");
    engine_context.results.push("src/prose/helpers.hpp");
    engine_context.results.push("src/prose/repository.cpp");
    engine_context.results.push("src/prose/repository.hpp");
    engine_context.results.push("src/prose/search.cpp");
    engine_context.results.push("src/prose/search.hpp");

    engine_context.query = cz::format("");
    spaces_are_wildcards_completion_filter(/*editor=*/nullptr, &context, &engine_context,
                                           /*selected_result=*/"src/prose/helpers.cpp",
                                           /*has_selected_result=*/true);
    CHECK(context.results == engine_context.results);
    CHECK(context.selected == 5);

    engine_context.query = cz::format("^src/pr");
    spaces_are_wildcards_completion_filter(/*editor=*/nullptr, &context, &engine_context,
                                           /*selected_result=*/{}, /*has_selected_result=*/false);
    CHECK(context.results == engine_context.results);
    CHECK(context.selected == 0);

    engine_context.query = cz::format("%pr");
    spaces_are_wildcards_completion_filter(/*editor=*/nullptr, &context, &engine_context,
                                           /*selected_result=*/{}, /*has_selected_result=*/false);
    CHECK(context.results == engine_context.results);
    CHECK(context.selected == 0);

    engine_context.query = cz::format("s p hel");
    spaces_are_wildcards_completion_filter(/*editor=*/nullptr, &context, &engine_context,
                                           /*selected_result=*/"src/prose/helpers.hpp",
                                           /*has_selected_result=*/true);
    REQUIRE(context.results.len == 2);
    CHECK(context.results[0] == "src/prose/helpers.cpp");
    CHECK(context.results[1] == "src/prose/helpers.hpp");
    CHECK(context.selected == 1);

    engine_context.query = cz::format("^src/prose/$");
    spaces_are_wildcards_completion_filter(/*editor=*/nullptr, &context, &engine_context,
                                           /*selected_result=*/"unlisted",
                                           /*has_selected_result=*/true);
    REQUIRE(context.results.len == 1);
    CHECK(context.results[0] == "src/prose/");
    CHECK(context.selected == 0);

    engine_context.query = cz::format("x$");
    spaces_are_wildcards_completion_filter(/*editor=*/nullptr, &context, &engine_context,
                                           /*selected_result=*/"unlisted",
                                           /*has_selected_result=*/true);
    REQUIRE(context.results.len == 0);
    CHECK(context.selected == 0);
}
