# PBL cross build. -Board unbound (fail-closed, default) or s32k338.
# While the generated layout (include/gen/pbl_layout.h) is not approved, the
# output goes to build_cand_<board> and never to build/: a relinked candidate
# must not overwrite the existing PBL output (Q10).
#
# -Bench selects PBL_BENCH_BRINGUP (direct primary-C0 boot, no HSE/journal,
# bench bring-up only) and -TxnBackend selects PBL_TXN_BACKEND. Both are real
# compile-time switches, so they are passed explicitly instead of relying on a
# hand-edited CMakeCache: a rebuild must not silently produce an unbound PBL.
param(
    [ValidateSet('unbound', 's32k338')][string]$Board = 'unbound',
    [string]$BuildDir,
    [switch]$Bench,
    # S1 bench: PBL_BENCH_DCM_BANK (boot bank from DCMSTAT); requires -Bench.
    [switch]$BenchDcmBank,
    [switch]$TxnBackend,
    [switch]$FactoryInstall,
    [string]$HseInstallSource
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($FactoryInstall -and ($Bench -or $HseInstallSource -notmatch '^0x[0-9A-Fa-f]+$')) {
    throw 'Factory candidate needs -HseInstallSource hex address and cannot use -Bench'
}
if (-not $FactoryInstall -and $HseInstallSource) { throw 'HSE source requires explicit -FactoryInstall' }
if ($BenchDcmBank -and -not $Bench) { throw 'PBL_BENCH_DCM_BANK is bench-only: use it with -Bench' }
if ($Bench -and ($Board -ne 's32k338')) {
    throw 'PBL_BENCH_BRINGUP needs -Board s32k338; the unbound port never jumps to C0'
}
$layoutHeader = Join-Path $projectRoot 'include\gen\pbl_layout.h'
$layoutText = [IO.File]::ReadAllText($layoutHeader)
if ($layoutText -notmatch '#define\s+PBL_LAYOUT_APPROVED\s+\((\d)U\)') { throw "PBL_LAYOUT_APPROVED not found in $layoutHeader" }
$approved = ($Matches[1] -eq '1')
if ($layoutText -notmatch '#define\s+PBL_LAYOUT_NAME\s+"([^"]+)"') { throw "PBL_LAYOUT_NAME not found in $layoutHeader" }
$layoutName = $Matches[1]
if ($layoutText -notmatch '#define\s+PBL_LAYOUT_SHA256\s+"([0-9a-fA-F]+)"') { throw "PBL_LAYOUT_SHA256 not found in $layoutHeader" }
$layoutSha = $Matches[1]
if (-not $BuildDir) {
    $suffix = ''
    if ($Bench) { $suffix += '_bench' }
    if ($BenchDcmBank) { $suffix += '_dcm' }
    if ($TxnBackend) { $suffix += '_txn' }
    if ($FactoryInstall) { $suffix += '_factory_install' }
    if ($approved) {
        $BuildDir = if ($Board -eq 'unbound') { 'build' } else { "build_$Board$suffix" }
    } else {
        $BuildDir = "build_cand_$Board$suffix"
    }
}
if ((-not $approved) -and ((Split-Path -Leaf $BuildDir) -eq 'build')) {
    throw "Layout $layoutName is not approved: refusing to build into build/ (existing PBL output)"
}
$ninjaCommand = Get-Command ninja -ErrorAction SilentlyContinue
if ($null -ne $ninjaCommand) {
    $ninjaPath = $ninjaCommand.Source
} else {
    $ninjaPath = 'C:\Users\Coldcc\AppData\Roaming\yt_config_tool\ninja-win\ninja.exe'
}
if (-not (Test-Path -LiteralPath $ninjaPath -PathType Leaf)) {
    throw 'Ninja was not found; install Ninja or add it to PATH'
}
$benchFlag = if ($Bench) { 'ON' } else { 'OFF' }
$txnFlag = if ($TxnBackend) { 'ON' } else { 'OFF' }
$dcmFlag = if ($BenchDcmBank) { 'ON' } else { 'OFF' }
$factoryFlag = if ($FactoryInstall) { 'ON' } else { 'OFF' }
$toolchain = Join-Path $projectRoot 'cmake\arm-gcc.cmake'
$buildDir = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $projectRoot $BuildDir }
$configureArgs = @(
    '-S', $projectRoot, '-B', $buildDir, '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninjaPath",
    "-DPBL_BOARD=$Board",
    "-DPBL_BENCH_BRINGUP=$benchFlag",
    "-DPBL_TXN_BACKEND=$txnFlag",
    "-DPBL_BENCH_DCM_BANK=$dcmFlag",
    "-DPBL_FACTORY_INSTALL=$factoryFlag",
    "-DPBL_HSE_INSTALL_SOURCE=$HseInstallSource"
)
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'PBL build failed' }
if (-not $approved) {
    [IO.File]::WriteAllText((Join-Path $buildDir 'CANDIDATE_LAYOUT_NOT_APPROVED.txt'),
        "Layout $layoutName is NOT approved (PBL_LAYOUT_APPROVED 0).`r`nOffline candidate build only: not a release artefact, do not flash.`r`n")
    Write-Output "WARN layout $layoutName not approved: candidate output in $buildDir"
}
# Machine-readable build identity: the factory packer refuses a PBL whose
# recorded binding does not match the binary it is about to publish.
$config = [ordered]@{
    board          = $Board
    bench_bringup  = [bool]$Bench
    txn_backend    = [bool]$TxnBackend
    bench_dcm_bank = [bool]$BenchDcmBank
    production     = $false
    factory_install = [bool]$FactoryInstall
    hse_install_source = $HseInstallSource
    hse_install_source_qualified = $false
    layout_name    = $layoutName
    layout_approved = $approved
    layout_sha256  = $layoutSha
    pbl_hex_sha256 = (Get-FileHash (Join-Path $buildDir 'pbl.hex')).Hash
    pbl_bin_sha256 = (Get-FileHash (Join-Path $buildDir 'pbl.bin')).Hash
    pbl_elf        = (Join-Path $buildDir 'pbl.elf')
}
$jsonPath = Join-Path $buildDir 'build_config.json'
$json = $config | ConvertTo-Json -Depth 4
# No BOM: the factory packer parses strict UTF-8.
[IO.File]::WriteAllText($jsonPath, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Output ("PBL OK  board {0}  bench {1}  txn {2}  {3}  pbl.hex SHA256 {4}" -f `
    $Board, $Bench, $TxnBackend, $buildDir, $config.pbl_hex_sha256)
