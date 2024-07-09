#!/bin/bash

# Run './build/debug/mag --client=ncurses $*' inside GDB.  This spawns a split screen
# layout inside tmux with GDB at the top and Mag at the bottom.  Arguments are forwarded
# through to Mag.  If this script is ran inside tmux it will take over the existing tmux
# window.  If it is ran from outside tmux, it will create a new tmux window automatically.
#
# GUI clients do not require a test harness because the graphics component
# doesn't interact with the command line.  But TUI clients such as ncurses take
# over the command line and thus cannot be used with a normal GDB instance.

set -e

# Not in a tmux session, so spawn one and recurse.
if [ -z "$TMUX" ]; then
    tmux new-session "$0" "$@"
    exit "$@"
fi

# At the bottom spawn the gdbserver (which instruments the binary).
tmux splitw -v -p 30 "gdbserver localhost:12345 ./build/debug/mag --client=ncurses $*"

# At the top spawn main gdb client (interactive debugger) and connect to the server.
tmux selectp -t 0
gdb -x debug-ncurses.gdb
