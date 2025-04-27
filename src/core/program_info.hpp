#pragma once

#include <cz/date.hpp>
#include <cz/str.hpp>

namespace mag {

extern char* program_name;
extern cz::Str program_dir;

/// The time the mag executable was created.
extern cz::Date program_date;

extern const char* const mag_build_directory;

extern const char* user_home_path;

}
