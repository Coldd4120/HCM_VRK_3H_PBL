$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
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
$buildDir = Join-Path $projectRoot 'build'
if (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt')) {
    & cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaPath"
} else {
    & cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DCMAKE_MAKE_PROGRAM=$ninjaPath"
}
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'PBL build failed' }
