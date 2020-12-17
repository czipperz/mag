#!/bin/bash

config="$1"
shift

cmake -DCMAKE_BUILD_TYPE="$config" "$@" ../..
cmake --build .
