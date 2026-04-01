# Copy freertos_skeleton.uf2 to the RP2040 RPI-RP2 USB drive (BOOTSEL).
# Usage (from this folder in PowerShell):
#   .\transfer_uf2.ps1
#   .\transfer_uf2.ps1 -Drive "E:\"
#   .\transfer_uf2.ps1 -Uf2Path ".\build\my_firmware.uf2"

param(
    [string] $Uf2Path = "",
    [string] $Drive = ""
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $Uf2Path) {
    $Uf2Path = Join-Path $scriptDir "build\freertos_skeleton.uf2"
}

if (-not (Test-Path -LiteralPath $Uf2Path)) {
    Write-Error "UF2 not found: $Uf2Path — run .\build_uf2.sh or build first."
    exit 1
}

if (-not $Drive) {
    $vol = Get-Volume -ErrorAction SilentlyContinue |
        Where-Object { $_.FileSystemLabel -eq "RPI-RP2" } |
        Select-Object -First 1
    if ($vol -and $vol.DriveLetter) {
        $Drive = $vol.DriveLetter + ":\"
    }
}

if (-not $Drive) {
    Write-Host "No RPI-RP2 volume found. Hold BOOTSEL, plug USB, then retry."
    Write-Host "Or pass a drive: .\transfer_uf2.ps1 -Drive 'E:\'"
    exit 1
}

$Drive = $Drive.TrimEnd("\") + "\"
if (-not (Test-Path -LiteralPath $Drive)) {
    Write-Error "Drive not accessible: $Drive"
    exit 1
}

$dest = Join-Path $Drive (Split-Path -Leaf $Uf2Path)
Copy-Item -LiteralPath $Uf2Path -Destination $dest -Force
Write-Host "Copied to $dest"
Write-Host "The board should reboot into the new firmware when the copy completes."
