#include <czt/test_base.hpp>

#include <cz/char_type.hpp>
#include <cz/file.hpp>
#include <cz/process.hpp>
#include <random>
#include "compression/zlib.hpp"
#include "compression/zstd.hpp"

using namespace mag;

static std::random_device dev;

static cz::String make_random_input(cz::Allocator allocator, size_t length) {
    std::mt19937 gen(dev());
    std::uniform_int_distribution<> dist(0, 255);

    cz::String input = {};
    input.reserve(allocator, length);
    for (size_t i = 0; i < length; ++i) {
        do {
            char ch = (char)(unsigned char)dist(gen);
            if (cz::is_print(ch)) {
                input.push(ch);
                break;
            }
        } while (1);
    }
    return input;
}

static cz::Input_File save_to_temp_file(char temp_file_buffer[L_tmpnam], cz::Str str) {
    {
        cz::Output_File output;
        CZ_ASSERT(output.open(temp_file_buffer));
        CZ_DEFER(output.close());

        int64_t result = output.write(str);
        CZ_ASSERT(result == (int64_t)str.len);
    }

    // TODO: open file in r/w mode and reset the head.
    cz::Input_File input;
    CZ_ASSERT(input.open(temp_file_buffer));
    return input;
}

static cz::Input_File run_compression_script(char compressed_file_buffer[L_tmpnam],
                                             const char* script,
                                             cz::Str input) {
    cz::Process process;

    char uncompressed_file_buffer[L_tmpnam];
    CZ_ASSERT(tmpnam(uncompressed_file_buffer));
    CZ_DEFER(cz::file::remove_file(uncompressed_file_buffer));

    {
        cz::Output_File compressed_file;
        CZ_ASSERT(compressed_file.open(compressed_file_buffer));

        cz::Input_File uncompressed_file = save_to_temp_file(uncompressed_file_buffer, input);
        CZ_DEFER(uncompressed_file.close());

        cz::Process_Options options;
        options.std_in = uncompressed_file;
        options.std_out = compressed_file;

        CZ_ASSERT(process.launch_script(script, options));
    }

    CZ_ASSERT(process.join() == 0);

    // TODO: open file in r/w mode and reset the head.
    cz::Input_File compressed_file;
    CZ_ASSERT(compressed_file.open(compressed_file_buffer));
    return compressed_file;
}

template <class DecompressionStream>
static void test_decompression(const char* script) {
    size_t length = GENERATE(0, 10, 1 << 12, 1 << 20, -1, -1, -1, -1);
    if (length == -1) {
        std::mt19937 gen(dev());
        std::uniform_int_distribution<> dist(0, 1 << 20);
        length = dist(gen);
    }

    cz::String input = make_random_input(cz::heap_allocator(), length);
    CZ_DEFER(input.drop(cz::heap_allocator()));

    char compressed_file_buffer[L_tmpnam];
    CZ_ASSERT(tmpnam(compressed_file_buffer));
    CZ_DEFER(cz::file::remove_file(compressed_file_buffer));

    cz::Input_File compressed_file = run_compression_script(compressed_file_buffer, script, input);
    CZ_DEFER(compressed_file.close());

    Contents contents = {};
    CZ_DEFER(contents.drop());

    REQUIRE(compression::process_file<DecompressionStream>(compressed_file, &contents) ==
            Load_File_Result::SUCCESS);

    cz::String result = contents.stringify(cz::heap_allocator());
    CZ_DEFER(result.drop(cz::heap_allocator()));
    CHECK(input == result);
}

template <class CompressionStream>
static void test_compression(const char* script) {
    size_t length = GENERATE(0, 10, 1 << 12, 1 << 20, -1, -1, -1, -1);
    if (length == -1) {
        std::mt19937 gen(dev());
        std::uniform_int_distribution<> dist(0, 1 << 20);
        length = dist(gen);
    }

    cz::String input = make_random_input(cz::heap_allocator(), length);
    CZ_DEFER(input.drop(cz::heap_allocator()));

    Contents contents;
    {
        char uncompressed_file_buffer_in[L_tmpnam];
        CZ_ASSERT(tmpnam(uncompressed_file_buffer_in));
        CZ_DEFER(cz::file::remove_file(uncompressed_file_buffer_in));

        CZ_ASSERT(cz::write_file(uncompressed_file_buffer_in, input));

        cz::Input_File uncompressed_file;
        CZ_ASSERT(uncompressed_file.open(uncompressed_file_buffer_in));
        CZ_DEFER(uncompressed_file.close());
        REQUIRE(compression::process_file<CompressionStream>(uncompressed_file, &contents) ==
                Load_File_Result::SUCCESS);
    }

    char uncompressed_file_buffer_out[L_tmpnam];
    CZ_ASSERT(tmpnam(uncompressed_file_buffer_out));
    CZ_DEFER(cz::file::remove_file(uncompressed_file_buffer_out));

    cz::String output = contents.stringify(cz::heap_allocator());
    CZ_DEFER(output.drop(cz::heap_allocator()));

    cz::Input_File file = run_compression_script(uncompressed_file_buffer_out, script, output);
    CZ_DEFER(file.close());

    cz::String result = {};
    CZ_DEFER(result.drop(cz::heap_allocator()));
    CZ_ASSERT(cz::read_to_string(file, cz::heap_allocator(), &result));
    CHECK(input == result);
}

#ifdef HAS_ZLIB
TEST_CASE("zlib / gzip decompression") {
    test_decompression<compression::zlib::DecompressionStream>("gzip -");
}
TEST_CASE("zlib / gzip compression") {
    test_compression<compression::zlib::CompressionStream>("gunzip -");
}
#endif

#ifdef HAS_ZSTD
TEST_CASE("zstd decompression") {
    test_decompression<compression::zstd::DecompressionStream>("zstd -");
}
TEST_CASE("zstd compression") {
    test_compression<compression::zstd::CompressionStream>("unzstd -");
}
#endif
