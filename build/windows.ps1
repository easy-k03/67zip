# Windows and ReactOS. ReactOS is this same Win32 port, not a separate one.
#
# MinGW (MSYS2):
#   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-zlib mingw-w64-x86_64-xz
#   ./build/windows.sh
#
# MSVC, from a Developer Prompt, once zlib and liblzma are on the link line:
#   cl /nologo /std:c++17 /EHsc /O2 /Fe:67zip.exe src\main.cpp src\port.cpp zlib.lib lzma.lib
#
# This PowerShell entry detects the compiler and stops with the exact
# package command when the Win32 toolchain is not on PATH. It does not
# pretend a missing compiler is a finished build.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$gpp = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $gpp) { $gpp = Get-Command x86_64-w64-mingw32-g++ -ErrorAction SilentlyContinue }
if ($gpp) {
    & $gpp.Source -std=c++17 -O2 -Wall -Wextra -o 67zip.exe src/main.cpp src/port.cpp -lz -llzma
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "67zip: built $root\67zip.exe"
    exit 0
}

$cl = Get-Command cl -ErrorAction SilentlyContinue
if ($cl) {
    & cl /nologo /std:c++17 /EHsc /O2 /Fe:67zip.exe src\main.cpp src\port.cpp zlib.lib lzma.lib
    exit $LASTEXITCODE
}

Write-Host "67zip: no Win32 C++ compiler on PATH."
Write-Host "67zip: MSYS2: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-zlib mingw-w64-x86_64-xz"
Write-Host "67zip: or run build/windows.sh from an environment that has the cross compiler."
exit 127
