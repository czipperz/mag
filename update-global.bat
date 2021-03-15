@echo off

if exist GTAGS (
    global -u
) else (
    gtags
)

exit /b 0
