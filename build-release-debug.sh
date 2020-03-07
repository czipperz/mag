#!/bin/bash

set -e

cd "$(dirname "$0")"
mkdir -p build/release-debug
cd build/release-debug
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ../.. >/dev/null
cmake --build .
