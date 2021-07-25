#!/bin/bash

set -e

cd "$(dirname "$0")"

./run-build.sh build/debug Debug

./build/debug/test --use-colour=no
