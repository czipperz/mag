#include "syntax/tokenize_ctest.hpp"

#include <cz/char_type.hpp>
#include "core/contents.hpp"
#include "core/eat.hpp"
#include "core/match.hpp"
#include "core/movement.hpp"
#include "core/token.hpp"

namespace mag {
namespace syntax {

namespace {
enum State {
    CTEST_HEADER,
};
}

// '1/5 Test #3: TestName'
// '          Start   30: TestName'
// '   1/5000 Test   #30: TestName'
static bool looking_at_test_file_result_start(Contents_Iterator iterator) {
    forward_through_whitespace(&iterator);

    if (eat_string(&iterator, "Start ")) {
        while (eat_character(&iterator, ' ')) {
        }
    } else {
        if (!eat_number(&iterator))
            return false;
        if (!eat_character(&iterator, '/'))
            return false;
        if (!eat_number(&iterator))
            return false;
        if (!eat_character(&iterator, ' '))
            return false;

        if (!eat_string(&iterator, "Test "))
            return false;
        while (eat_character(&iterator, ' ')) {
        }

        if (!eat_character(&iterator, '#'))
            return false;
    }

    if (!eat_number(&iterator))
        return false;

    return looking_at(iterator, ": ");
    // Everything after the colon is considered the test name and can be whatever the user wants.
}

static bool looking_at_path_character(Contents_Iterator iterator) {
    if (iterator.at_eob())
        return false;
    char ch = iterator.get();
    switch (ch) {
    case '.':
    case '/':
    case '-':
    case '_':
    case CZ_ALNUM_CASES:
        return true;
    default:
        return false;
    }
}

// 'my/path.cpp(144): Entering test case "test case 1"'
// 'my/path.cpp(144): Leaving test case "test case 1"; testing time: 1ns'
static bool looking_at_test_case_header(Contents_Iterator iterator) {
    if (!looking_at_path_character(iterator))
        return false;
    do
        iterator.advance();
    while (looking_at_path_character(iterator));

    if (!eat_character(&iterator, '('))
        return false;
    if (!eat_number(&iterator))
        return false;
    return looking_at(iterator, "): Entering test case \"") ||
           looking_at(iterator, "): Leaving test case \"");
}

bool ctest_next_token(Contents_Iterator* iterator, Token* token, uint64_t* state) {
    while (looking_at(*iterator, '\n')) {
        iterator->advance();
    }
    if (iterator->at_eob()) {
        return false;
    }

    token->start = iterator->position;

    if (looking_at_test_file_result_start(*iterator)) {
        token->type = Token_Type::TEST_LOG_FILE_HEADER;
    } else if (looking_at_test_case_header(*iterator)) {
        token->type = Token_Type::TEST_LOG_TEST_CASE_HEADER;
    } else {
        token->type = Token_Type::DEFAULT;
    }
    end_of_line(iterator);

    token->end = iterator->position;
    return true;
}

}
}
