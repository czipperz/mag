#include <cz/buffer_array.hpp>
#include <cz/defer.hpp>
#include <cz/heap_string.hpp>
#include <czt/test_base.hpp>
#include "client.hpp"
#include "command_macros.hpp"
#include "server.hpp"
#include "token.hpp"

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
    void set_tokenizer(Tokenizer tokenizer);

    /// `input` should have `|` to represent cursors;
    /// other characters will be inserted into the buffer.
    void setup(cz::Str input);

    /// `input` should have `|` to represent cursor points
    /// and `(` or `)` to specify the the cursor's mark.
    /// For example `a(bc|d` will make a cursor with mark at position 1 and point at position 3.
    void setup_region(cz::Str input);

    /// Stringify the buffer's contents, adding `|` for each cursor.
    cz::String stringify();

    /// Run a command.
    void run(Command_Function command);

    cz::Allocator allocator() { return buffer_array.allocator(); }
};

}
