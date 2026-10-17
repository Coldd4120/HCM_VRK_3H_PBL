$ErrorActionPreference = 'Stop'
$zig = 'E:\work\VRK\01_FIRMWARE\HCM_VRK_3H_OUT\TOOLS\srs_test_deps\ziglang\zig.exe'
if (-not (Test-Path -LiteralPath $zig)) { throw 'Zig host compiler not found' }
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build\test_pbl_core.exe'
& $zig cc '-std=c11' '-Wall' '-Wextra' '-Werror' '-O2' '-I' (Join-Path $root 'include') (Join-Path $root 'src\pbl_core.c') (Join-Path $root 'tests\test_pbl_core.c') '-o' $out
if ($LASTEXITCODE -ne 0) { throw 'Host compile failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
