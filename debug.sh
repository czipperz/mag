#!/bin/bash

tmux splitw -v -p 30 "gdbserver localhost:12345 ./build/debug/mag $*"
tmux selectp -t 0
gdb -x debug.gdb
