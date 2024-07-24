#pragma once

#include "src/file.hpp"

#ifdef HAS_ZSTD

namespace mag {
namespace compression {

Load_File_Result load_zstd_file(Buffer* buffer, cz::Input_File file);

}
}

#endif
