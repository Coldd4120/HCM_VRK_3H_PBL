$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$report = Join-Path $projectRoot 'build\production_readiness.json'
& python (Join-Path $PSScriptRoot 'audit_production_readiness.py') --out $report --quiet
if ($LASTEXITCODE -ne 0) {
    throw "PBL production source gates failed; review $report"
}
throw 'PBL target-board and OEM release evidence is not yet bound to this build entry'
