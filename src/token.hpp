#pragma once

#include <stdint.h>

namespace mag {

struct Face;

namespace Token_Type_ {
enum Token_Type : uint64_t {
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

    CSS_PROPERTY,
    CSS_ELEMENT_SELECTOR,
    CSS_ID_SELECTOR,
    CSS_CLASS_SELECTOR,
    CSS_PSEUDO_SELECTOR,

    CUSTOM = 0x8000000000000000,
    CUSTOM_FOREGROUND_IS_COLOR = 0x4000000000000000,
    CUSTOM_BACKGROUND_IS_COLOR = 0x2000000000000000,
    CUSTOM_FACE_INVISIBLE = 0x1000000000000000,
};

Token_Type encode(Face face);
Face decode(Token_Type type);

}
using Token_Type_::Token_Type;

struct Token {
    uint64_t start;
    uint64_t end;
    Token_Type type;
};

}
