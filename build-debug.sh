#!/bin/bash

set -e

cd "$(dirname "$0")"

./build-wrapper.sh build/debug Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1

if [ ! -e compile_commands.json ]; then
    ln -s "$(pwd)/build/debug/compile_commands.json" .
fi

./run-tests.sh
