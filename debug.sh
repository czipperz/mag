#!/bin/bash

tmux splitw -v -p 50 "gdbserver localhost:12345 ./build/debug/mag"
tmux selectp -t 0
gdb -x debug.gdb
