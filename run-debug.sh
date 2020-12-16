#!/bin/sh

if [ -e ./build/debug/mag ]; then
    ./build/debug/mag "$@"
else
    cd ./build/debug
    ./Debug/mag "$@"
fi
