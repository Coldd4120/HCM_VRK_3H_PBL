$ErrorActionPreference = 'Stop'
# Thin wrapper: the suites and mutation tests live in run_host_tests.py.
& python (Join-Path $PSScriptRoot 'run_host_tests.py') @args
if ($LASTEXITCODE -ne 0) { throw 'PBL host tests failed' }
