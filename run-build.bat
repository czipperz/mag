@echo off

SET VARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"

if not exist %VARS% (
    echo Could not find vcvars64.bat at %VARS%
    echo If you have Visual Studio installed, please edit mag/run-build.bat to reflect the correct path to vcvars64.bat.
    echo Otherwise, please install Visual Studio.
    exit /b 1
)

@call %VARS% >NUL

SET config=%1
shift

cmake -GNinja -DCMAKE_BUILD_TYPE="%config%" %* ../..
cmake --build .
