param(
    [string]$Version = "0.1.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ReleaseRoot = Join-Path $ProjectRoot "release"
$PackageName = "portable-c-shell-$Version"
$Staging = Join-Path $ReleaseRoot $PackageName
$Archive = Join-Path $ReleaseRoot "$PackageName.zip"

$ResolvedProject = [IO.Path]::GetFullPath($ProjectRoot)
$ResolvedRelease = [IO.Path]::GetFullPath($ReleaseRoot)
$ResolvedStaging = [IO.Path]::GetFullPath($Staging)
if (-not $ResolvedRelease.StartsWith($ResolvedProject,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not $ResolvedStaging.StartsWith($ResolvedRelease,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Release paths escaped the project root"
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build_host.ps1")
    if ($LASTEXITCODE -ne 0) { throw "Release validation build failed" }
}

New-Item -ItemType Directory -Path $ReleaseRoot -Force | Out-Null
if (Test-Path -LiteralPath $Staging) {
    Remove-Item -LiteralPath $Staging -Recurse -Force
}
if (Test-Path -LiteralPath $Archive) {
    Remove-Item -LiteralPath $Archive -Force
}
New-Item -ItemType Directory -Path $Staging | Out-Null

foreach ($File in @("CMakeLists.txt", "README.md", "IMPLEMENTATION_PLAN.md")) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot $File) -Destination $Staging
}
foreach ($Directory in @("docs", "shell", "scripts")) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot $Directory) `
        -Destination $Staging -Recurse
}
if (Test-Path -LiteralPath (Join-Path $ProjectRoot "build_host_artifacts")) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot "build_host_artifacts") `
        -Destination $Staging -Recurse
}

Compress-Archive -LiteralPath $Staging -DestinationPath $Archive `
    -CompressionLevel Optimal
$Hash = Get-FileHash -Algorithm SHA256 -LiteralPath $Archive
$HashLine = "$($Hash.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($Archive))"
Set-Content -LiteralPath (Join-Path $ReleaseRoot "SHA256SUMS") `
    -Value $HashLine -Encoding ascii

Write-Host "Release archive: $Archive"
Write-Host $HashLine
