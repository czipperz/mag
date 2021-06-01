Push-Location $(Split-Path -Parent -Path $MyInvocation.MyCommand.Definition)

try {
    ./run-build.ps1 build/release-debug RelWithDebInfo
    if (!$?) { exit 1 }
} finally {
    Pop-Location
}
