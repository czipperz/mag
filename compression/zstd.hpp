#pragma once

#include "compression/compression_general.hpp"

#ifdef HAS_ZSTD

namespace mag {
namespace compression {
namespace zstd {

struct DecompressionStream {
    static size_t recommended_in_buffer_size();
    static size_t recommended_out_buffer_size();

    bool init();
    void drop();

    /// Decompress a chunk of input.  Advances the `in_cursor` and `out_cursor` to
    /// reflect the input that was processed and the output that was produced.  When the
    /// input stream has ended, use `last_input = true` to force inflation to flush.
    /// Continue decompression until `Compression_Result::DONE` is reached.
    Compression_Result decompress_chunk(const void** in_cursor,
                                        const void* in_end,
                                        void** out_cursor,
                                        void* out_end,
                                        bool last_input);

    // Actual type: ZSTD_DStream* stream;
    void* stream;
};

}
}
}

#endif
