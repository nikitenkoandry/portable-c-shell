param(
    [string]$BuildRoot = "$env:TEMP\serial_shell_analyzer_src",
    [string]$Generator = "MinGW Makefiles"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $BuildRoot "build"

if (Test-Path -LiteralPath $BuildRoot) {
    Remove-Item -LiteralPath $BuildRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $ProjectRoot "CMakeLists.txt") -Destination $BuildRoot
Copy-Item -LiteralPath (Join-Path $ProjectRoot "shell") -Destination $BuildRoot -Recurse

cmake -S $BuildRoot -B $BuildDir -G $Generator `
    -DSH_ENABLE_GCC_ANALYZER=ON -DSH_WARNINGS_AS_ERRORS=ON
if ($LASTEXITCODE -ne 0) { throw "Analyzer configure failed: $LASTEXITCODE" }
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw "GCC analyzer failed: $LASTEXITCODE" }
