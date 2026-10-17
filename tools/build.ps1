# PBL cross build. -Board unbound (fail-closed, default) or s32k338.
# While the generated layout (include/gen/pbl_layout.h) is not approved, the
# output goes to build_cand_<board> and never to build/: a relinked candidate
# must not overwrite the existing PBL output (Q10).
param(
    [ValidateSet('unbound', 's32k338')][string]$Board = 'unbound',
    [string]$BuildDir
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$layoutHeader = Join-Path $projectRoot 'include\gen\pbl_layout.h'
$layoutText = [IO.File]::ReadAllText($layoutHeader)
if ($layoutText -notmatch '#define\s+PBL_LAYOUT_APPROVED\s+\((\d)U\)') { throw "PBL_LAYOUT_APPROVED not found in $layoutHeader" }
$approved = ($Matches[1] -eq '1')
if ($layoutText -notmatch '#define\s+PBL_LAYOUT_NAME\s+"([^"]+)"') { throw "PBL_LAYOUT_NAME not found in $layoutHeader" }
$layoutName = $Matches[1]
if (-not $BuildDir) {
    if ($approved) {
        $BuildDir = if ($Board -eq 'unbound') { 'build' } else { "build_$Board" }
    } else {
        $BuildDir = "build_cand_$Board"
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
$toolchain = Join-Path $projectRoot 'cmake\arm-gcc.cmake'
$buildDir = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $projectRoot $BuildDir }
if (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt')) {
    & cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaPath" "-DPBL_BOARD=$Board"
} else {
    & cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DCMAKE_MAKE_PROGRAM=$ninjaPath" "-DPBL_BOARD=$Board"
}
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'PBL build failed' }
if (-not $approved) {
    [IO.File]::WriteAllText((Join-Path $buildDir 'CANDIDATE_LAYOUT_NOT_APPROVED.txt'),
        "Layout $layoutName is NOT approved (PBL_LAYOUT_APPROVED 0).`r`nOffline candidate build only: not a release artefact, do not flash.`r`n")
    Write-Output "WARN layout $layoutName not approved: candidate output in $buildDir"
}
Write-Output ("PBL OK  board {0}  {1}  pbl.hex SHA256 {2}" -f $Board, $buildDir, (Get-FileHash (Join-Path $buildDir 'pbl.hex')).Hash)
