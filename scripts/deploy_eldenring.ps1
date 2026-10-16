# Automated deployment and verification script for Elden Ring mc-core adapter
param(
    [string]$SourceDll = "D:\code\universal-modder\mc-core\build\bin\eldenring\dinput8.dll",
    [string]$TargetDir = "C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game"
)

$targetDll = Join-Path $TargetDir "dinput8.dll"

if (!(Test-Path $SourceDll)) {
    Write-Error "Source DLL not found: $SourceDll"
    exit 1
}

Write-Output "Deploying: $SourceDll -> $targetDll"
Copy-Item -Path $SourceDll -Destination $targetDll -Force

$srcHash = (Get-FileHash -Path $SourceDll -Algorithm SHA256).Hash
$dstHash = (Get-FileHash -Path $targetDll -Algorithm SHA256).Hash

Write-Output "Source SHA256:      $srcHash"
Write-Output "Destination SHA256: $dstHash"

if ($srcHash -eq $dstHash) {
    Write-Output "SUCCESS: Binaries verified identical."
    exit 0
} else {
    Write-Error "MISMATCH: Hashes do not match!"
    exit 2
}
