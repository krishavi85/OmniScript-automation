param(
    [Parameter(Mandatory = $true)] [string]$Package,
    [Parameter(Mandatory = $true)] [string]$InstallDir
)

$ErrorActionPreference = "Stop"
$packagePath = (Resolve-Path $Package).Path
$target = [IO.Path]::GetFullPath($InstallDir)
$stage = Join-Path $env:TEMP ("OmniStemUpdate-" + [Guid]::NewGuid())
$backup = "$target.rollback"

try {
    Expand-Archive $packagePath $stage -Force
    $manifestPath = Join-Path $stage "release-manifest.json"
    if (-not (Test-Path $manifestPath)) { throw "Update manifest is missing" }
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
    foreach ($entry in $manifest.files) {
        $file = Join-Path $stage $entry.name
        if (-not (Test-Path $file)) { throw "Update file is missing: $($entry.name)" }
        $actual = (Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne [string]$entry.sha256) { throw "Hash mismatch: $($entry.name)" }
    }

    Stop-Process -Name "OmniStem Studio" -Force -ErrorAction SilentlyContinue
    Stop-Process -Name "OmniStemWorker" -Force -ErrorAction SilentlyContinue
    Remove-Item $backup -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $target) { Move-Item $target $backup }
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Copy-Item (Join-Path $stage "*") $target -Recurse -Force
    Remove-Item $backup -Recurse -Force -ErrorAction SilentlyContinue
} catch {
    Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $backup) { Move-Item $backup $target }
    throw
} finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
