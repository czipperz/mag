#!/bin/bash

set -e

if [ -e ./build/debug/test ]; then
    ./build/debug/test --use-colour=no
else
    ./build/debug/Debug/test --use-colour=no
fi
