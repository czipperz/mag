#include "test_runner.hpp"
#include "syntax/tokenize_general.hpp"

namespace mag {

Test_Runner::Test_Runner() {
    buffer_array.init();

    server = {};
    server.init();

    Buffer test_buffer = {};
    test_buffer.type = Buffer::TEMPORARY;
    test_buffer.name = cz::Str("*test*").clone(cz::heap_allocator());
    server.editor.create_buffer(test_buffer);

    client = server.make_client();
    server.setup_async_context(&client);

    set_tokenizer(syntax::general_next_token);
}

void Test_Runner::set_tokenizer(Tokenizer tokenizer) {
    Window_Unified* window = client.selected_window();
    WITH_WINDOW_BUFFER(window);
    buffer->mode.next_token = tokenizer;
}

Test_Runner::~Test_Runner() {
    client.drop();
    server.drop();
    buffer_array.drop();
}

void Test_Runner::setup(cz::Str input) {
    WITH_SELECTED_BUFFER(&client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::String contents = {};

    size_t offset = 0;
    for (size_t i = 0; i < input.len; ++i) {
        if (input[i] == '|') {
            window->cursors.reserve(cz::heap_allocator(), 1);
            Cursor cursor = {};
            cursor.point = i - offset;
            window->cursors.push(cursor);
            ++offset;
        } else {
            contents.reserve(transaction.value_allocator(), 1);
            contents.push(input[i]);
        }
    }

    if (window->cursors.len > 1) {
        window->cursors.remove(0);
    }

    Edit edit;
    edit.value = SSOStr::from_constant(contents);
    edit.position = 0;
    edit.flags = Edit::INSERT;
    transaction.push(edit);

    transaction.commit(&client);

    window->change_index = buffer->changes.len;
}

cz::String Test_Runner::stringify() {
    cz::String output = {};

    WITH_CONST_SELECTED_BUFFER(&client);

    Contents_Iterator it = buffer->contents.start();
    for (size_t i = 0; i < window->cursors.len; ++i) {
        uint64_t end = window->cursors[i].point;
        while (it.position < end) {
            output.reserve(allocator(), 1);
            output.push(it.get());
            it.advance();
        }
        output.reserve(allocator(), 1);
        output.push('|');
    }

    while (!it.at_eob()) {
        output.reserve(allocator(), 1);
        output.push(it.get());
        it.advance();
    }

    return output;
}

void Test_Runner::run(Command_Function command) {
    Command_Source source = {};
    source.client = &client;
    command(&server.editor, source);
}

}
