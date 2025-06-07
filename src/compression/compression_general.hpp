#pragma once

#include <stddef.h>
#include <cz/defer.hpp>
#include <cz/file.hpp>
#include <cz/heap.hpp>
#include "core/contents.hpp"
#include "core/file.hpp"

namespace mag {
namespace compression {

namespace Compression_Result_ {
enum Compression_Result {
    SUCCESS = 1,    ///< Some input was processed.
    DONE = 2,       ///< The end of the stream has been reached.
    NEED_DICT = 3,  ///< Intervention by the caller is required to provide a dictionary.

    /// Either no input was processed, or `last_input=true` and is still more input to process.
    BUFFERING = 4,

    /// Fatal errors:
    ERROR_INVALID_INPUT = -1,  ///< Input buffer contains an invalid compressed stream.
    ERROR_INVALID_STATE = -2,  ///< Programming error due to memory corruption or invalid inputs.
    ERROR_OUT_OF_MEMORY = -3,  ///< Internal allocation failure.
    ERROR_OTHER = -4,

#define ANY_COMPRESSION_RESULT_ERROR(X) \
    X(ERROR_INVALID_INPUT) X(ERROR_INVALID_STATE) X(ERROR_OUT_OF_MEMORY) X(ERROR_OTHER)
};
}
using Compression_Result_::Compression_Result;

/// Compress or decompress the input file and put the result into `contents`.
template <class Stream>
Load_File_Result process_file(cz::Input_File input, Contents* contents) {
    Stream stream;
    if (stream.init() < 0)
        return Load_File_Result::FAILURE;
    CZ_DEFER(stream.drop());

    cz::String in = {};
    CZ_DEFER(in.drop(cz::heap_allocator()));
    in.reserve_exact(cz::heap_allocator(), stream.recommended_in_buffer_size());

    cz::String out = {};
    CZ_DEFER(out.drop(cz::heap_allocator()));
    out.reserve_exact(cz::heap_allocator(), stream.recommended_out_buffer_size());
    char* out_end = out.buffer + out.cap;

    while (1) {
        int64_t read_len = input.read(in.buffer, in.cap);
        if (read_len < 0) {
            return Load_File_Result::FAILURE;
        } else if (read_len == 0) {
            const void* in_cursor = in.buffer;
            const char* const in_end = in.buffer;
            while (1) {
                void* out_cursor = out.buffer;
                Compression_Result result = stream.process_chunk(&in_cursor, in_end, &out_cursor,
                                                                 out_end, /*last_input=*/true);
                contents->append({out.buffer, size_t((char*)out_cursor - out.buffer)});

                if (!(result == Compression_Result::SUCCESS ||
                      result == Compression_Result::BUFFERING)) {
                    return result >= 0 ? Load_File_Result::SUCCESS : Load_File_Result::FAILURE;
                }
            }
        }

        const void* in_cursor = in.buffer;
        const char* const in_end = in.buffer + read_len;
        do {
            void* out_cursor = out.buffer;
            Compression_Result result = stream.process_chunk(&in_cursor, in_end, &out_cursor,
                                                             out_end, /*last_input=*/false);
            contents->append({out.buffer, size_t((char*)out_cursor - out.buffer)});

            if (result == Compression_Result::SUCCESS) {
                // Repeat.
            } else if (result == Compression_Result::BUFFERING) {
                // If no output was produced then we've hit an error.
                if (out_cursor == out.buffer) {
                    CZ_DEBUG_ASSERT(false && "Please investigate BUFFERING result");
                    return Load_File_Result::FAILURE;
                }
            } else {
                // Stop on an error, if we're done, etc.
                return result >= 0 ? Load_File_Result::SUCCESS : Load_File_Result::FAILURE;
            }
        } while (in_cursor != in_end);
    }
}

}
}
