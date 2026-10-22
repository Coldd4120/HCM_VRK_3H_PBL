# PBL production entry.
#
# Two independent things happen here, each with its own output location so a
# task-cache run never writes into the PBL checkout:
#   1. the static readiness audit (always run, report path selectable);
#   2. an optional PBL build into an explicit -BuildDir (no Bench, no Txn).
# The entry still refuses to declare a production PBL: target-board evidence,
# HSE/bank facts and OEM release approval are not bound to any build yet
# (W20/W23 own closing that gate). Keeping the refusal here means a caller
# cannot mistake a green static audit for a releasable boot loader.
param(
    [string]$ReportPath,
    [string]$BuildDir,
    [string]$AppRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'HCM_VRK_3H'),
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

if ($BuildDir) {
    if (-not [IO.Path]::IsPathRooted($BuildDir)) {
        throw "-BuildDir must be an absolute path: '$BuildDir'"
    }
    $buildDirFull = $BuildDir
}
else {
    $buildDirFull = Join-Path $projectRoot 'build'
}
if (-not $ReportPath) {
    $ReportPath = Join-Path $buildDirFull 'production_readiness.json'
}
elseif (-not [IO.Path]::IsPathRooted($ReportPath)) {
    throw "-ReportPath must be an absolute path: '$ReportPath'"
}
$reportDir = Split-Path -Parent $ReportPath
if (-not (Test-Path -LiteralPath $reportDir)) {
    New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
}
Write-Output ("readiness report {0}" -f $ReportPath)

& python (Join-Path $PSScriptRoot 'audit_production_readiness.py') `
    --app-root $AppRoot --out $ReportPath --quiet
$auditExit = $LASTEXITCODE

if ($auditExit -ne 0) {
    throw "PBL production source gates failed; review $ReportPath"
}

if ($SkipBuild) {
    Write-Output 'SkipBuild: readiness audit only.'
}
elseif (-not $BuildDir) {
    Write-Output 'No -BuildDir given: static audit only, nothing was compiled.'
}
else {
    Write-Output ("building PBL into {0}" -f $buildDirFull)
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') `
        -Board s32k338 -BuildDir $buildDirFull
    if ($LASTEXITCODE -ne 0) { throw "PBL build failed in $buildDirFull" }
}

throw 'PBL target-board and OEM release evidence is not yet bound to this build entry'
