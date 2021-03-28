#!/bin/bash

set -e

cd "$(dirname "$0")"

directory="$1"
config="$2"
shift
shift

mkdir -p "$directory"
cd "$directory"

cmake -DCMAKE_BUILD_TYPE="$config" "$@" ../.. >/dev/null
cmake --build . --config "$config"
