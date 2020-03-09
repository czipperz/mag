#!/bin/bash

set -e

cd "$(dirname "$0")"
mkdir -p build/tracy
cd build/tracy
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTRACY_ENABLE=1 ../.. >/dev/null
cmake --build .
