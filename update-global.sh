#!/bin/bash

if [ -e GTAGS ]; then
    global -u
else
    gtags
fi

exit 0
