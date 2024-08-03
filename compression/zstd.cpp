#include "zstd.hpp"

#ifdef HAS_ZSTD

#include <zstd.h>
#include <cz/defer.hpp>
#include <cz/file.hpp>

namespace mag {
namespace compression {
namespace zstd {

size_t DecompressionStream::recommended_in_buffer_size() {
    return ZSTD_DStreamInSize();
}
size_t DecompressionStream::recommended_out_buffer_size() {
    return ZSTD_DStreamOutSize();
}

bool DecompressionStream::init() {
    stream = ZSTD_createDStream();
    return stream != nullptr;
}
void DecompressionStream::drop() {
    ZSTD_freeDStream((ZSTD_DStream*)stream);
}

Compression_Result DecompressionStream::decompress_chunk(const void** in_cursor,
                                                         const void* in_end,
                                                         void** out_cursor,
                                                         void* out_end,
                                                         bool last_input) {
    ZSTD_outBuffer out;
    out.dst = *out_cursor;
    out.size = (const char*)out_end - (const char*)*out_cursor;
    out.pos = 0;
    ZSTD_inBuffer in;
    in.src = *in_cursor;
    in.size = (const char*)in_end - (const char*)*in_cursor;
    in.pos = 0;

    size_t result = ZSTD_decompressStream((ZSTD_DStream*)stream, &out, &in);

    *(const char**)in_cursor += in.pos;
    *(char**)out_cursor += out.pos;

    if (result == 0)
        return Compression_Result::DONE;
    else if (ZSTD_isError(result))
        return Compression_Result::ERROR_OTHER;
    else
        return Compression_Result::SUCCESS;
}

}
}
}

#endif
