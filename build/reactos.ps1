# ReactOS uses the Windows toolchain and the Win32 branch.
# There is no ReactOS-specific source. This forwards to windows.ps1.
$ErrorActionPreference = "Stop"
& "$PSScriptRoot\windows.ps1"
exit $LASTEXITCODE
