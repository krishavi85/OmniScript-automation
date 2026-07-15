param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir
)

$ErrorActionPreference = "Stop"
$target = [IO.Path]::GetFullPath($InstallDir)
$backup = "$target.rollback"
if (-not (Test-Path $backup)) {
    throw "No rollback snapshot exists at $backup"
}

Stop-Process -Name "OmniStem Studio" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "OmniStemWorker" -Force -ErrorAction SilentlyContinue
$failed = "$target.failed-" + (Get-Date -Format "yyyyMMddHHmmss")
if (Test-Path $target) {
    Move-Item $target $failed
}
Move-Item $backup $target
Write-Output "Rollback restored: $target"
if (Test-Path $failed) {
    Write-Output "Failed update preserved for inspection: $failed"
}
