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

    PREPROCESSOR_KEYWORD,

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

    HTML_TAG_NAME,
    HTML_ATTRIBUTE_NAME,
    HTML_AMPERSAND_CODE,

    DIRECTORY_COLUMN,
    DIRECTORY_SELECTED_COLUMN,
    DIRECTORY_FILE_TIME,
    DIRECTORY_FILE_DIRECTORY,
    DIRECTORY_FILE_NAME,

    SEARCH_COMMAND,
    SEARCH_FILE_NAME,
    SEARCH_FILE_LINE,
    SEARCH_FILE_COLUMN,
    SEARCH_RESULT,

    SPLASH_LOGO,
    SPLASH_KEY_BIND,

    // Special value representing the number of values in the enum.
    length,

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

struct Contents_Iterator;

/// A `Tokenizer` parses a `Buffer`'s `Contents` into a sequence of `Token`s.
///
/// Whitespace should be skipped at the start of the implementation.
///
/// `state` starts as `0` and can be any value; it is the
/// only state you are allowed to maintain between runs.
///
/// Returns `true` if a token was found and `false` if end of file was reached.
using Tokenizer = bool (*)(Contents_Iterator* iterator /* in/out */,
                           Token* token /* out */,
                           uint64_t* state /* in/out */);

}
