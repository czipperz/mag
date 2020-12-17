#!/bin/bash

set -e

cd "$(dirname "$0")"

directory="$1"
shift

root="$(pwd)"

mkdir -p "$directory"
cd "$directory"

if [ "$OSTYPE" == "msys" ]; then
    "$root"/run-build.bat "$@"
else
    "$root"/run-build.sh "$@"
fi
