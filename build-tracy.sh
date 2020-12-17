#!/bin/bash

set -e

"$(dirname "$0")"/build-wrapper.sh build/tracy RelWithDebInfo -DTRACY_ENABLE=1
