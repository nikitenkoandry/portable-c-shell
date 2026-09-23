param(
    [string]$BuildRoot = "$env:TEMP\serial_shell_build_src",
    [string]$Generator = "MinGW Makefiles"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Artifacts = Join-Path $ProjectRoot "build_host_artifacts"

if (Test-Path -LiteralPath $BuildRoot) {
    Remove-Item -LiteralPath $BuildRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $BuildRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $ProjectRoot "CMakeLists.txt") -Destination $BuildRoot
Copy-Item -LiteralPath (Join-Path $ProjectRoot "shell") -Destination $BuildRoot -Recurse

cmake -S $BuildRoot -B (Join-Path $BuildRoot "build") -G $Generator
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build (Join-Path $BuildRoot "build")
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }
ctest --test-dir (Join-Path $BuildRoot "build") --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE" }

New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildRoot "build\host_terminal_shell.exe") -Destination $Artifacts -Force
Copy-Item -LiteralPath (Join-Path $BuildRoot "build\tcp_loopback_shell.exe") -Destination $Artifacts -Force
Copy-Item -LiteralPath (Join-Path $BuildRoot "build\sh_memory_report.exe") -Destination $Artifacts -Force
Copy-Item -LiteralPath (Join-Path $BuildRoot "build\sh_lookup_benchmark.exe") -Destination $Artifacts -Force

Write-Host "Built host shell:"
Write-Host (Join-Path $Artifacts "host_terminal_shell.exe")
