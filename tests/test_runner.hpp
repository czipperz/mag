#include <cz/buffer_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap_string.hpp>
#include <czt/test_base.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "server.hpp"

namespace mag {

struct Test_Runner {
    Server server;
    Client client;
    cz::Buffer_Array buffer_array;

    Test_Runner();
    ~Test_Runner();
    Test_Runner(const Test_Runner&) = delete;
    Test_Runner& operator=(const Test_Runner&) = delete;

    /// Set a custom tokenizer.
    void set_tokenizer(bool (*tokenizer)(Contents_Iterator*, Token*, uint64_t*));

    /// `input` should have `|` to represent cursors;
    /// other characters will be inserted into the buffer.
    void setup(cz::Str input);

    /// Stringify the buffer's contents, adding `|` for each cursor.
    cz::String stringify();

    /// Run a command.
    void run(Command_Function command);

    cz::Allocator allocator() { return buffer_array.allocator(); }
};

}
