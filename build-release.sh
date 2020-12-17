#!/bin/bash

set -e

"$(dirname "$0")"/build-wrapper.sh build/release Release
