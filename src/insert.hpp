#pragma once

namespace mag {
struct Buffer;
struct Window_Unified;
struct SSOStr;

void insert(Buffer* buffer, Window_Unified* window, SSOStr value);
void insert_char(Buffer* buffer, Window_Unified* window, char code);

}
