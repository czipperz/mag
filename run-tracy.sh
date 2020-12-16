#!/bin/sh

if [ -e ./build/tracy/mag ]; then
    ./build/tracy/mag "$@"
else
    cd ./build/tracy
    ./Debug/mag "$@"
fi
