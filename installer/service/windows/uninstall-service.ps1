<#
.SYNOPSIS
    Removes the TerminalSim Windows service.
#>
[CmdletBinding()]
param(
    [string] $ServiceName = "TerminalSim"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue)) {
    Write-Host "Service '$ServiceName' is not registered."
    exit 0
}

Stop-Service -Name $ServiceName -Force -ErrorAction SilentlyContinue
& sc.exe delete $ServiceName | Out-Null
Write-Host "Service '$ServiceName' removed."
