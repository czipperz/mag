#include "test_runner.hpp"

#include <cz/sort.hpp>
#include "syntax/tokenize_general.hpp"

namespace mag {

Test_Runner::Test_Runner() {
    buffer_array.init();

    server = {};
    server.init();

    Buffer test_buffer = create_temp_buffer("test");
    test_buffer.read_only = false;
    server.editor.create_buffer(test_buffer);

    client = server.make_client();
    server.setup_async_context(&client);

    set_tokenizer(syntax::general_next_token);

    client.window->total_rows = 128;
    client.window->total_cols = 128;
}

void Test_Runner::set_tokenizer(Tokenizer tokenizer) {
    Window_Unified* window = client.selected_window();
    WITH_WINDOW_BUFFER(window, &client);
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

    if (contents.len > 0) {
        Edit edit;
        edit.value = SSOStr::from_constant(contents);
        edit.position = 0;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    transaction.commit(&client);

    window->change_index = buffer->changes.len;
}

void Test_Runner::setup_region(cz::Str input) {
    WITH_SELECTED_BUFFER(&client);

    Transaction transaction;
    transaction.init(buffer);
    CZ_DEFER(transaction.drop());

    cz::String contents = {};

    size_t offset = 0;
    cz::Vector<uint64_t> points = {}, marks = {};
    CZ_DEFER(points.drop(cz::heap_allocator()));
    CZ_DEFER(marks.drop(cz::heap_allocator()));
    for (size_t i = 0; i < input.len; ++i) {
        if (input[i] == '|') {
            points.reserve(cz::heap_allocator(), 1);
            points.push(i - offset);
            ++offset;
        } else if (input[i] == '(' || input[i] == ')') {
            marks.reserve(cz::heap_allocator(), 1);
            marks.push(i - offset);
            ++offset;
        } else {
            contents.reserve(transaction.value_allocator(), 1);
            contents.push(input[i]);
        }
    }

    CZ_ASSERT(points.len == marks.len);
    for (size_t i = 0; i < points.len; ++i) {
        window->cursors.reserve(cz::heap_allocator(), 1);
        Cursor cursor = {};
        cursor.point = points[i];
        cursor.mark = marks[i];
        window->cursors.push(cursor);
    }
    window->show_marks = true;

    if (window->cursors.len > 1) {
        window->cursors.remove(0);
    }

    if (contents.len > 0) {
        Edit edit;
        edit.value = SSOStr::from_constant(contents);
        edit.position = 0;
        edit.flags = Edit::INSERT;
        transaction.push(edit);
    }

    transaction.commit(&client);

    window->change_index = buffer->changes.len;
}

cz::String Test_Runner::stringify() {
    WITH_CONST_SELECTED_BUFFER(&client);
    return stringify(window, buffer);
}

cz::String Test_Runner::stringify(const Window_Unified* window, const Buffer* buffer) {
    cz::String output = {};

    struct Separator {
        uint64_t position;
        char ch;
        bool operator<(const Separator& other) const {
            if (position != other.position)
                return position < other.position;
            return ch < other.ch;
        }
    };

    cz::Vector<Separator> separators = {};
    separators.reserve_exact(cz::heap_allocator(),
                             window->cursors.len * (window->show_marks ? 2 : 1));
    CZ_DEFER(separators.drop(cz::heap_allocator()));
    for (size_t i = 0; i < window->cursors.len; ++i) {
        separators.push({window->cursors[i].point, '|'});
    }
    if (window->show_marks) {
        for (size_t i = 0; i < window->cursors.len; ++i) {
            if (window->cursors[i].mark < window->cursors[i].point)
                separators.push({window->cursors[i].mark, '('});
            else
                separators.push({window->cursors[i].mark, ')'});
        }
    }
    cz::sort(separators);

    Contents_Iterator it = buffer->contents.start();
    for (size_t i = 0; i < separators.len; ++i) {
        uint64_t end = separators[i].position;
        while (it.position < end) {
            output.reserve(cz::heap_allocator(), 1);
            output.push(it.get());
            it.advance();
        }
        output.reserve(cz::heap_allocator(), 1);
        output.push(separators[i].ch);
    }

    while (!it.at_eob()) {
        output.reserve(cz::heap_allocator(), 1);
        output.push(it.get());
        it.advance();
    }

    return output;
}

cz::String Test_Runner::slice(uint64_t start, uint64_t end) {
    Window_Unified* window = client.selected_window();
    WITH_CONST_WINDOW_BUFFER(window, &client);

    cz::String string = {};
    string.reserve_exact(buffer_array.allocator(), end - start);
    Contents_Iterator it = buffer->contents.iterator_at(start);
    buffer->contents.slice_into(it, end, &string);
    return string;
}

void Test_Runner::run(Command_Function command) {
    Command_Source source = {};
    source.client = &client;
    command(&server.editor, source);
}

////////////////////////////////////////////////////////////////////////////////
// Tokenization utils
////////////////////////////////////////////////////////////////////////////////

bool Test_Runner::TToken::operator==(const TToken& other) const {
    return str == other.str &&  //
           token.start == other.token.start && token.end == other.token.end &&
           token.type == other.token.type;
}
bool Test_Runner::TToken::operator!=(const TToken& other) const {
    return !(*this == other);
}

cz::Vector<Test_Runner::TToken> Test_Runner::tokenize(Tokenizer tokenizer) {
    Window_Unified* window = client.selected_window();
    WITH_CONST_WINDOW_BUFFER(window, &client);

    if (!tokenizer) {
        tokenizer = buffer->mode.next_token;
    }

    cz::Vector<TToken> tokens = {};

    Contents_Iterator it = buffer->contents.start();
    Token token = INVALID_TOKEN;
    uint64_t state = 0;
    while (tokenizer(&it, &token, &state)) {
        token.check_valid(buffer->contents.len);
        tokens.reserve(buffer_array.allocator(), 1);
        tokens.push({{}, token});
        token = INVALID_TOKEN;
    }

    // Allocate strings after first loop to allow the vector to reallocate.
    for (TToken& token : tokens) {
        token.str = slice(token.token.start, token.token.end);
    }

    return tokens;
}

void Test_Runner::tokenize_print_tests(Tokenizer tokenizer) {
    cz::Buffer_Array::Save_Point sp = buffer_array.save();
    CZ_DEFER(buffer_array.restore(sp));

    cz::Vector<TToken> tokens = tokenize(tokenizer);
    cz::print("    auto tokens = tr.tokenize();\n");
    cz::print("    REQUIRE(tokens.len == ", tokens.len, ");\n");
    for (size_t i = 0; i < tokens.len; ++i) {
        cz::print("    CHECK(tokens[", i, "] == Test_Runner::TToken", tokens[i], ");\n");
    }
}

void Test_Runner::open_temp_file(cz::Str temp_name, cz::Option<cz::Str> dir) {
    client.set_selected_buffer(server.editor.create_buffer(create_temp_buffer(temp_name, dir)));
}

static void validate_window(Window* window) {
    if (window->tag != Window::UNIFIED) {
        Window_Split* split = (Window_Split*)window;
        CZ_ASSERT(split->first);
        CZ_ASSERT(split->second);
        CZ_ASSERT(split->first->parent == split);
        CZ_ASSERT(split->second->parent == split);
        validate_window(split->first);
        validate_window(split->second);
    }
}

void Test_Runner::validate_client_layout() const {
    CZ_ASSERT(client.window);
    validate_window(client.window);
}

static void stringify_window_layout(cz::Allocator allocator,
                                    cz::String* string,
                                    Window* window,
                                    uint32_t depth) {
    cz::append(allocator, string, cz::many(' ', depth * 2));
    if (window->tag == Window::UNIFIED) {
        Window_Unified* unified = (Window_Unified*)window;
        WITH_CONST_BUFFER_HANDLE(unified->buffer_handle);
        buffer->render_name(allocator, string);
        cz::append(allocator, string, '\n');
    } else {
        Window_Split* split = (Window_Split*)window;
        cz::append(allocator, string,
                   (window->tag == Window::VERTICAL_SPLIT ? "Vertical:\n" : "Horizontal:\n"));
        stringify_window_layout(allocator, string, split->first, depth + 1);
        stringify_window_layout(allocator, string, split->second, depth + 1);
    }
}

cz::String Test_Runner::stringify_client_layout() {
    validate_client_layout();
    cz::String string = {};
    stringify_window_layout(allocator(), &string, client.window, 0);
    return string;
}

}

namespace cz {
void append(cz::Allocator allocator, cz::String* string, const mag::Test_Runner::TToken& token) {
    cz::append(allocator, string, "{", cz::dbg(token.str), ", ", token.token, "}");
}
}
