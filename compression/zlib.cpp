#include "zstd.hpp"

#ifdef HAS_ZLIB

#include <zlib.h>
#include <cz/defer.hpp>
#include <cz/file.hpp>

namespace mag {
namespace compression {

Load_File_Result load_zlib_file(Buffer* buffer, cz::Input_File file) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.next_in = 0;
    stream.avail_in = 0;
    // Add together the amount of memory to be used (8..15) and detection scheme (32 = automatic).
    int window_bits = 15 + 32;
    int ret = inflateInit2(&stream, window_bits);
    if (ret != Z_OK) {
        return Load_File_Result::FAILURE;
    }
    CZ_DEFER(inflateEnd(&stream));

    char input_buffer[4096];

    cz::String out = {};
    CZ_DEFER(out.drop(cz::heap_allocator()));
    out.reserve_exact(cz::heap_allocator(), 4096);

    while (1) {
        int64_t read_len = file.read(input_buffer, sizeof(input_buffer));
        if (read_len < 0) {
            return Load_File_Result::FAILURE;
        }
        int flags = read_len == 0 ? Z_FINISH : Z_NO_FLUSH;

        stream.next_in = (unsigned char*)input_buffer;
        stream.avail_in = read_len;

        do {
            stream.next_out = (unsigned char*)out.buffer + out.len;
            stream.avail_out = out.cap - out.len;

            ret = inflate(&stream, flags);

            if (ret == Z_OK) {
                buffer->contents.append({out.buffer, out.cap - stream.avail_out});
                out.len = 0;
            } else if (ret == Z_STREAM_END) {
                buffer->contents.append({out.buffer, out.cap - stream.avail_out});
                out.len = 0;
                return Load_File_Result::SUCCESS;
            } else if (ret == Z_BUF_ERROR) {
                out.len = out.cap - stream.avail_out;
                out.reserve_exact(cz::heap_allocator(), out.cap * 2);
            } else {
                return Load_File_Result::FAILURE;
            }
        } while (stream.avail_out == 0);

        if (read_len == 0) {
            return Load_File_Result::SUCCESS;
        }
    }
}

}
}

#endif
