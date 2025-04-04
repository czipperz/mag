#include "zlib.hpp"

#ifdef HAS_ZLIB

#include <zlib.h>
#include <cz/defer.hpp>
#include <cz/file.hpp>

namespace mag {
namespace compression {
namespace zlib {

////////////////////////////////////////////////////////////////////////////////
// Forward declarations
////////////////////////////////////////////////////////////////////////////////

static Compression_Result translate(int ret);

////////////////////////////////////////////////////////////////////////////////
// CompressionStream
////////////////////////////////////////////////////////////////////////////////

size_t CompressionStream::recommended_in_buffer_size() {
    return 4096;
}
size_t CompressionStream::recommended_out_buffer_size() {
    return 4096;
}

Compression_Result CompressionStream::init() {
    memset(&stream, 0, sizeof(stream));
    int level = Z_DEFAULT_COMPRESSION;
    int window_bits = 15 /* 2^15 byte window */ + 16 /* gzip header */;
    int mem_level = 8;  // high memory compression state
    int ret = deflateInit2(&stream, level, Z_DEFLATED, window_bits, mem_level, Z_DEFAULT_STRATEGY);
    return translate(ret);
}

void CompressionStream::drop() {
    deflateEnd(&stream);
}

Compression_Result CompressionStream::process_chunk(const void** in_cursor,
                                                    const void* in_end,
                                                    void** out_cursor,
                                                    void* out_end,
                                                    bool last_input) {
    stream.next_in = (unsigned char*)*in_cursor;
    stream.avail_in = (const char*)in_end - (const char*)*in_cursor;
    stream.next_out = (unsigned char*)*out_cursor;
    stream.avail_out = (const char*)out_end - (const char*)*out_cursor;

    int flags = last_input ? Z_FINISH : Z_NO_FLUSH;
    int ret = deflate(&stream, flags);

    *in_cursor = stream.next_in;
    *out_cursor = stream.next_out;

    return translate(ret);
}

////////////////////////////////////////////////////////////////////////////////
// DecompressionStream
////////////////////////////////////////////////////////////////////////////////

size_t DecompressionStream::recommended_in_buffer_size() {
    return 4096;
}
size_t DecompressionStream::recommended_out_buffer_size() {
    return 4096;
}

Compression_Result DecompressionStream::init() {
    memset(&stream, 0, sizeof(stream));
    // Add together the amount of memory to be used (8..15) and detection scheme (32 = automatic).
    int window_bits = 15 + 32;
    int ret = inflateInit2(&stream, window_bits);
    return translate(ret);
}

void DecompressionStream::drop() {
    inflateEnd(&stream);
}

Compression_Result DecompressionStream::process_chunk(const void** in_cursor,
                                                      const void* in_end,
                                                      void** out_cursor,
                                                      void* out_end,
                                                      bool last_input) {
    stream.next_in = (unsigned char*)*in_cursor;
    stream.avail_in = (const char*)in_end - (const char*)*in_cursor;
    stream.next_out = (unsigned char*)*out_cursor;
    stream.avail_out = (const char*)out_end - (const char*)*out_cursor;

    int flags = last_input ? Z_FINISH : Z_NO_FLUSH;
    int ret = inflate(&stream, flags);

    *in_cursor = stream.next_in;
    *out_cursor = stream.next_out;

    return translate(ret);
}

////////////////////////////////////////////////////////////////////////////////
// Utilities
////////////////////////////////////////////////////////////////////////////////

static Compression_Result translate(int ret) {
    switch (ret) {
    case Z_OK:
        return Compression_Result::SUCCESS;
    case Z_STREAM_END:
        return Compression_Result::DONE;
    case Z_NEED_DICT:
        return Compression_Result::NEED_DICT;
    case Z_BUF_ERROR:
        return Compression_Result::BUFFERING;
    case Z_DATA_ERROR:
        return Compression_Result::ERROR_INVALID_INPUT;
    case Z_STREAM_ERROR:
        return Compression_Result::ERROR_INVALID_STATE;
    case Z_MEM_ERROR:
        return Compression_Result::ERROR_OUT_OF_MEMORY;
    default:
        return Compression_Result::ERROR_OTHER;
    }
}

}
}
}

#endif
