param(
    [string]$BuildRoot = "$env:TEMP\serial_shell_coverage_src",
    [string]$Generator = "MinGW Makefiles",
    [double]$MinimumLinePercent = 90.0,
    [double]$MinimumBranchPercent = 85.0
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $BuildRoot "build"
$ObjectDir = Join-Path $BuildDir "CMakeFiles\sh_shell.dir\shell\src"
$Artifacts = Join-Path $ProjectRoot "build_host_artifacts"

if (Test-Path -LiteralPath $BuildRoot) {
    Remove-Item -LiteralPath $BuildRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $ProjectRoot "CMakeLists.txt") -Destination $BuildRoot
Copy-Item -LiteralPath (Join-Path $ProjectRoot "shell") -Destination $BuildRoot -Recurse

cmake -S $BuildRoot -B $BuildDir -G $Generator `
    -DSH_ENABLE_COVERAGE=ON -DSH_WARNINGS_AS_ERRORS=ON
if ($LASTEXITCODE -ne 0) { throw "Coverage configure failed: $LASTEXITCODE" }
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw "Coverage build failed: $LASTEXITCODE" }
ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Coverage tests failed: $LASTEXITCODE" }

Push-Location $ObjectDir
try {
    Get-ChildItem -Filter "*.gcov.json.gz" -ErrorAction SilentlyContinue |
        Remove-Item -Force
    Get-ChildItem -Filter "*.gcda" | ForEach-Object {
        & gcov --json-format --branch-probabilities --branch-counts $_.FullName |
            Out-Null
        if ($LASTEXITCODE -ne 0) { throw "gcov failed for $($_.Name)" }
    }

    [long]$LineTotal = 0
    [long]$LineCovered = 0
    [long]$BranchTotal = 0
    [long]$BranchCovered = 0

    Get-ChildItem -Filter "*.gcov.json.gz" | ForEach-Object {
        $FileStream = [IO.File]::OpenRead($_.FullName)
        try {
            $Gzip = [IO.Compression.GzipStream]::new(
                $FileStream, [IO.Compression.CompressionMode]::Decompress)
            try {
                $Reader = [IO.StreamReader]::new($Gzip)
                try { $Document = $Reader.ReadToEnd() | ConvertFrom-Json }
                finally { $Reader.Dispose() }
            }
            finally { $Gzip.Dispose() }
        }
        finally { $FileStream.Dispose() }

        foreach ($Source in $Document.files) {
            foreach ($Line in $Source.lines) {
                $LineTotal++
                if ([long]$Line.count -gt 0) { $LineCovered++ }
                foreach ($Branch in $Line.branches) {
                    $BranchTotal++
                    if ([long]$Branch.count -gt 0) { $BranchCovered++ }
                }
            }
        }
    }
}
finally {
    Pop-Location
}

$LinePercent = if ($LineTotal) { 100.0 * $LineCovered / $LineTotal } else { 100.0 }
$BranchPercent = if ($BranchTotal) { 100.0 * $BranchCovered / $BranchTotal } else { 100.0 }
$Report = @(
    "Portable shell core coverage"
    ("Lines:    {0}/{1} ({2:N2}%)" -f $LineCovered, $LineTotal, $LinePercent)
    ("Branches: {0}/{1} ({2:N2}%)" -f $BranchCovered, $BranchTotal, $BranchPercent)
    ("Required: lines >= {0:N2}%, branches >= {1:N2}%" -f `
        $MinimumLinePercent, $MinimumBranchPercent)
) -join [Environment]::NewLine

New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null
$ReportPath = Join-Path $Artifacts "coverage_report.txt"
Set-Content -LiteralPath $ReportPath -Value $Report -Encoding ascii
Write-Host $Report
Write-Host "Report: $ReportPath"

if ($LinePercent -lt $MinimumLinePercent) {
    throw "Line coverage gate failed"
}
if ($BranchPercent -lt $MinimumBranchPercent) {
    throw "Branch coverage gate failed"
}
