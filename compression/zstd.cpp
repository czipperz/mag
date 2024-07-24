#include "zstd.hpp"

#ifdef HAS_ZSTD

#include <zstd.h>
#include <cz/defer.hpp>
#include <cz/file.hpp>

namespace mag {
namespace compression {

Load_File_Result load_zstd_file(Buffer* buffer, cz::Input_File file) {
    cz::String input = {};
    CZ_DEFER(input.drop(cz::heap_allocator()));
    if (!cz::read_to_string(file, cz::heap_allocator(), &input))
        return Load_File_Result::FAILURE;

    unsigned long long decompressed_size = ZSTD_getFrameContentSize(input.buffer, input.len);
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN ||
        decompressed_size == ZSTD_CONTENTSIZE_ERROR)
        return Load_File_Result::FAILURE;

    cz::String output = {};
    CZ_DEFER(output.drop(cz::heap_allocator()));
    output.reserve_exact(cz::heap_allocator(), decompressed_size);

    decompressed_size = ZSTD_decompress(output.buffer, output.cap, input.buffer, input.len);
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN ||
        decompressed_size == ZSTD_CONTENTSIZE_ERROR)
        return Load_File_Result::FAILURE;

    output.len = decompressed_size;
    buffer->contents.append(output);

    buffer->read_only = true;

    return Load_File_Result::SUCCESS;
}

}
}

#endif
