#!/bin/bash

set -e

cd "$(dirname "$0")"

directory="$1"
shift

root="$(pwd)"

mkdir -p "$directory"
cd "$directory"

if [ "$OSTYPE" == "msys" ]; then
    VC_INSTALL_DIR="${VC_INSTALL_DIR:-'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community'}"
    VARS="$VC_INSTALL_DIR"'\VC\Auxiliary\Build\vcvars64.bat'

    if [ ! -e "$VARS" ]; then
        echo "Could not find vcvars64.bat at:"
        echo "    $VARS"
        echo
        echo "If you have Visual Studio installed, please specify VC_INSTALL_DIR to"
        echo "reflect the correct path to vcvars64.bat.  It is assumed to be:"
        echo "    $VC_INSTALL_DIR"
        echo
        echo "Otherwise, please install Visual Studio."
        exit 1
    fi

    VARS="$VARS" "$root"/run-build.bat "$@"
else
    "$root"/run-build.sh "$@"
fi
