<#
.SYNOPSIS
    Registers TerminalSim as a Windows service.

.DESCRIPTION
    Optional helper. The installer ships this script but does NOT run it
    automatically. Operators choose when to register the service.

    Run from an elevated PowerShell prompt:
        .\install-service.ps1 -InstallDir "C:\Program Files\TerminalSim"

.PARAMETER InstallDir
    Directory containing terminal_simulation.exe.

.PARAMETER ServiceName
    Service name to register. Defaults to TerminalSim.

.PARAMETER StartupType
    Manual (default) or Automatic.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $InstallDir,
    [string] $ServiceName = "TerminalSim",
    [ValidateSet("Manual", "Automatic", "Disabled")] [string] $StartupType = "Manual"
)

$ErrorActionPreference = "Stop"

$exe = Join-Path $InstallDir "bin\terminal_simulation.exe"
if (-not (Test-Path $exe)) {
    throw "terminal_simulation.exe not found at $exe"
}

if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) {
    Write-Host "Service '$ServiceName' already exists; updating binPath."
    & sc.exe config $ServiceName binPath= "`"$exe`"" start= $StartupType.ToLower()
} else {
    Write-Host "Registering service '$ServiceName'."
    New-Service -Name $ServiceName `
                -BinaryPathName "`"$exe`"" `
                -DisplayName "TerminalSim Server" `
                -Description "TerminalSim container terminal simulation server." `
                -StartupType $StartupType | Out-Null
}

Write-Host "Done. Start with: Start-Service $ServiceName"
