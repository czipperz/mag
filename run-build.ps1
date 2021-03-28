Param(
    [Parameter(Position=0, Mandatory)]
    [String] $directory,
    [Parameter(Position=1, Mandatory)]
    [String] $config,
    [Parameter(Position=2, ValueFromRemainingArguments)]
    [String[]] $rest
)

$ErrorActionPreference = "Stop"

Push-Location $(Split-Path -Parent -Path $MyInvocation.MyCommand.Definition)

try {
    if (-not(Test-Path $directory)) {
        New-Item $directory -ItemType Directory >$null
    }
    cd $directory

    # If VCToolsVersion is set assume we already ran vcvars.
    if (-not(Test-Path env:VCToolsVersion)) {
        # If VCINSTALLDIR is set as an environment variable then use it.
        if (Test-Path env:VCINSTALLDIR) {
            $VCINSTALLDIR = $env:VCINSTALLDIR
        } else {
            $VCINSTALLDIR = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC"
        }

        $VARS = "$VCINSTALLDIR\Auxiliary\Build\vcvars64.bat"

        if (-not(Test-Path $VARS)) {
            echo "Could not find vcvars64.bat at:"
            echo "    $VARS"
            echo ""
            echo "If you have Visual Studio installed, please specify VCINSTALLDIR to"
            echo "reflect the correct path to vcvars64.bat.  It is assumed to be:"
            echo "    $VCINSTALLDIR"
            echo ""
            echo "Otherwise, please install Visual Studio."
            exit 1
        }

        cmd /c """$VARS"" >NUL & set" |
            foreach {
                if ($_ -match "=") {
                    $v = $_.split("="); set-item -force -path "ENV:\$($v[0])"  -value "$($v[1])"
                }
            }

        Write-Host "Visual Studio 2019 Command Prompt variables set.`n" -ForegroundColor Yellow
    }

    cmake -GNinja -DCMAKE_BUILD_TYPE="$config" $rest ../.. >$null
    if (!$?) { exit 1 }
    cmake --build . --config "$config"
    if (!$?) { exit 1 }
} finally {
    Pop-Location
}
