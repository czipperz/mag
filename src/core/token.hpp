#pragma once

#include <stdint.h>
#include <cz/string.hpp>

namespace mag {

struct Face;

// clang-format off
#define MAG_TOKEN_TYPES(MAG_TOKEN_TYPE)                                       \
    MAG_TOKEN_TYPE(DEFAULT)                                                   \
    MAG_TOKEN_TYPE(KEYWORD)                                                   \
    MAG_TOKEN_TYPE(TYPE)                                                      \
    MAG_TOKEN_TYPE(PUNCTUATION)                                               \
    /* Parenthesis and braces are tracked via OPEN_PAIR and CLOSE_PAIR.       \
       Some languages (e.g. bash, C preprocessor) use keywords for            \
       if/for/etc.  In these languages we use DIVIDER_PAIR to represent       \
       'else' keywords that act as both a CLOSE_PAIR and OPEN_PAIR.  */       \
    MAG_TOKEN_TYPE(OPEN_PAIR)                                                 \
    MAG_TOKEN_TYPE(DIVIDER_PAIR)                                              \
    MAG_TOKEN_TYPE(CLOSE_PAIR)                                                \
    MAG_TOKEN_TYPE(COMMENT)                                                   \
    MAG_TOKEN_TYPE(DOC_COMMENT)                                               \
    MAG_TOKEN_TYPE(STRING)                                                    \
    MAG_TOKEN_TYPE(IDENTIFIER)                                                \
    MAG_TOKEN_TYPE(NUMBER)                                                    \
                                                                              \
    MAG_TOKEN_TYPE(PREPROCESSOR_KEYWORD)                                      \
    MAG_TOKEN_TYPE(PREPROCESSOR_IF)                                           \
    MAG_TOKEN_TYPE(PREPROCESSOR_ELSE)                                         \
    MAG_TOKEN_TYPE(PREPROCESSOR_ENDIF)                                        \
                                                                              \
    MAG_TOKEN_TYPE(MERGE_START)                                               \
    MAG_TOKEN_TYPE(MERGE_MIDDLE)                                              \
    MAG_TOKEN_TYPE(MERGE_END)                                                 \
                                                                              \
    MAG_TOKEN_TYPE(TITLE)                                                     \
    MAG_TOKEN_TYPE(CODE)                                                      \
    MAG_TOKEN_TYPE(LINK_TITLE)                                                \
    MAG_TOKEN_TYPE(LINK_HREF)                                                 \
                                                                              \
    MAG_TOKEN_TYPE(PATCH_COMMIT_CONTEXT)                                      \
    MAG_TOKEN_TYPE(PATCH_FILE_CONTEXT)                                        \
    MAG_TOKEN_TYPE(PATCH_REMOVE)                                              \
    MAG_TOKEN_TYPE(PATCH_ADD)                                                 \
    MAG_TOKEN_TYPE(PATCH_NEUTRAL)                                             \
    MAG_TOKEN_TYPE(PATCH_ANNOTATION)                                          \
                                                                              \
    MAG_TOKEN_TYPE(GIT_REBASE_TODO_COMMAND)                                   \
    MAG_TOKEN_TYPE(GIT_REBASE_TODO_SHA)                                       \
    MAG_TOKEN_TYPE(GIT_REBASE_TODO_COMMIT_MESSAGE)                            \
                                                                              \
    MAG_TOKEN_TYPE(PROCESS_ESCAPE_SEQUENCE)                                   \
    MAG_TOKEN_TYPE(PROCESS_BOLD)                                              \
    MAG_TOKEN_TYPE(PROCESS_ITALICS)                                           \
    MAG_TOKEN_TYPE(PROCESS_BOLD_ITALICS)                                      \
                                                                              \
    MAG_TOKEN_TYPE(CSS_PROPERTY)                                              \
    MAG_TOKEN_TYPE(CSS_ELEMENT_SELECTOR)                                      \
    MAG_TOKEN_TYPE(CSS_ID_SELECTOR)                                           \
    MAG_TOKEN_TYPE(CSS_CLASS_SELECTOR)                                        \
    MAG_TOKEN_TYPE(CSS_PSEUDO_SELECTOR)                                       \
                                                                              \
    MAG_TOKEN_TYPE(HTML_TAG_NAME)                                             \
    MAG_TOKEN_TYPE(HTML_ATTRIBUTE_NAME)                                       \
    MAG_TOKEN_TYPE(HTML_AMPERSAND_CODE)                                       \
                                                                              \
    MAG_TOKEN_TYPE(DIRECTORY_COLUMN)                                          \
    MAG_TOKEN_TYPE(DIRECTORY_SELECTED_COLUMN)                                 \
    MAG_TOKEN_TYPE(DIRECTORY_FILE_TIME)                                       \
    MAG_TOKEN_TYPE(DIRECTORY_FILE_DIRECTORY)                                  \
    MAG_TOKEN_TYPE(DIRECTORY_FILE_NAME)                                       \
                                                                              \
    MAG_TOKEN_TYPE(SEARCH_COMMAND)                                            \
    MAG_TOKEN_TYPE(SEARCH_FILE_NAME)                                          \
    MAG_TOKEN_TYPE(SEARCH_FILE_LINE)                                          \
    MAG_TOKEN_TYPE(SEARCH_FILE_COLUMN)                                        \
    MAG_TOKEN_TYPE(SEARCH_RESULT)                                             \
                                                                              \
    MAG_TOKEN_TYPE(SPLASH_LOGO)                                               \
    MAG_TOKEN_TYPE(SPLASH_KEY_BIND)                                           \
                                                                              \
    MAG_TOKEN_TYPE(BLAME_HASH)                                                \
    MAG_TOKEN_TYPE(BLAME_COMMITTER)                                           \
    MAG_TOKEN_TYPE(BLAME_DATE)                                                \
    MAG_TOKEN_TYPE(BLAME_CONTENTS)                                            \
                                                                              \
    MAG_TOKEN_TYPE(BUILD_LOG_FILE_HEADER)                                     \
    MAG_TOKEN_TYPE(BUILD_LOG_LINK)                                            \
                                                                              \
    MAG_TOKEN_TYPE(BUFFER_TEMPORARY_NAME)
// clang-format on

namespace Token_Type_ {
enum Token_Type : uint64_t {
#define X(name) name,
    MAG_TOKEN_TYPES(X)
#undef X

    // Special value representing the number of values in the enum.
    length,

    CUSTOM = 0x8000000000000000,
    CUSTOM_FOREGROUND_IS_COLOR = 0x4000000000000000,
    CUSTOM_BACKGROUND_IS_COLOR = 0x2000000000000000,
    CUSTOM_FACE_INVISIBLE = 0x1000000000000000,
};

/// Token_Type -> string lookup table for simple token types.
extern const char* const names[/*Token_Type::length*/];

Token_Type encode(Face face);
Face decode(Token_Type type);

}
using Token_Type_::Token_Type;

struct Token {
    uint64_t start;
    uint64_t end;
    Token_Type type;

    bool is_valid(uint64_t contents_len) const;
    void assert_valid(uint64_t contents_len) const;
};

constexpr Token INVALID_TOKEN = {(uint64_t)-1, (uint64_t)-1, Token_Type::length};

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

namespace cz {
void append(cz::Allocator allocator, cz::String* string, const mag::Token& token);
void append(cz::Allocator allocator, cz::String* string, mag::Token_Type type);
}
