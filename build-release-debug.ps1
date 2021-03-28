Push-Location $(Split-Path -Parent -Path $MyInvocation.MyCommand.Definition)

try {
    ./run-build.ps1 build/release-debug RelWithDebInfo
} finally {
    Pop-Location
}
