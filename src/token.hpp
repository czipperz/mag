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
    STRING,
    IDENTIFIER,
};
}
using Token_Type_::Token_Type;

struct Token {
    uint64_t start;
    uint64_t end;
    Token_Type type;
};

}
