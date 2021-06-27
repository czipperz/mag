#pragma once

#include <stdint.h>

namespace mag {

struct Contents_Iterator;
struct Token;

namespace syntax {

bool git_commit_edit_message_next_token(Contents_Iterator* iterator,
                                        Token* token,
                                        uint64_t* state);

}
}
