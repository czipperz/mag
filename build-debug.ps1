Push-Location $(Split-Path -Parent -Path $MyInvocation.MyCommand.Definition)

try {
    ./run-build.ps1 build/debug Debug
    if (!$?) { exit 1 }

    ./build/debug/test --use-colour=no
    if (!$?) { exit 1 }
} finally {
    Pop-Location
}
