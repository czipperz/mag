#!/bin/bash

set -e

./build-release-debug.sh
perf record -g ./build/release-debug/mag "$@"
