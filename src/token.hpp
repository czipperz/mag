#pragma once

#include <stdint.h>

namespace mag {

namespace Token_Type_ {
enum Token_Type {
    DEFAULT,
    KEYWORD,
    TYPE,
    PUNCTUATION,
    OPEN_PAIR,
    CLOSE_PAIR,
    COMMENT,
    DOC_COMMENT,
    STRING,
    IDENTIFIER,
    NUMBER,

    MERGE_START,
    MERGE_MIDDLE,
    MERGE_END,

    TITLE,
    CODE,

    PATCH_REMOVE,
    PATCH_ADD,
    PATCH_NEUTRAL,
    PATCH_ANNOTATION,

    GIT_REBASE_TODO_COMMAND,
    GIT_REBASE_TODO_SHA,
    GIT_REBASE_TODO_COMMIT_MESSAGE,

    PROCESS_ESCAPE_SEQUENCE,
    PROCESS_BOLD,
    PROCESS_ITALICS,
    PROCESS_BOLD_ITALICS,
};
}
using Token_Type_::Token_Type;

struct Token {
    uint64_t start;
    uint64_t end;
    Token_Type type;
};

}
