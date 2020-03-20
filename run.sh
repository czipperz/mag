#!/bin/bash

set -e

tmux new-window "gdbserver localhost:12346 ./build/debug/mag $*"
tmux select-window -t 1
gdb -x run.gdb
