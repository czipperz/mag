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

    TITLE,
    CODE,

    PATCH_REMOVE,
    PATCH_ADD,
    PATCH_NEUTRAL,
    PATCH_ANNOTATION,

    GIT_REBASE_TODO_COMMAND,
    GIT_REBASE_TODO_SHA,
    GIT_REBASE_TODO_COMMIT_MESSAGE,
};
}
using Token_Type_::Token_Type;

struct Token {
    uint64_t start;
    uint64_t end;
    Token_Type type;
};

}
