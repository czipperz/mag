@echo off

@call "%VARS%" >NUL

SET config=%1
shift

cmake -GNinja -DCMAKE_BUILD_TYPE="%config%" %* ../..
cmake --build .
