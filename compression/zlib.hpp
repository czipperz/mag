#pragma once

#include "src/file.hpp"

#ifdef HAS_ZLIB

namespace mag {
namespace compression {

Load_File_Result load_zlib_file(Buffer* buffer, cz::Input_File in);

}
}

#endif
