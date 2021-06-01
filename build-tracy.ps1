Push-Location $(Split-Path -Parent -Path $MyInvocation.MyCommand.Definition)

try {
    ./run-build.ps1 build/tracy RelWithDebInfo -DTRACY_ENABLE=1
    if (!$?) { exit 1 }
} finally {
    Pop-Location
}
