param(
    [Parameter(Mandatory = $true)]
    [string]$Installer
)

$ErrorActionPreference = "Stop"
$installerPath = (Resolve-Path $Installer).Path
$installDir = Join-Path $env:RUNNER_TEMP "OmniStemDeploymentValidation"
Remove-Item $installDir -Recurse -Force -ErrorAction SilentlyContinue

$process = Start-Process $installerPath -ArgumentList @(
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART",
    "/DIR=$installDir"
) -PassThru -Wait
if ($process.ExitCode -ne 0) {
    throw "Installer failed with exit code $($process.ExitCode)"
}

$required = @(
    (Join-Path $installDir "OmniStem Studio.exe"),
    (Join-Path $installDir "worker\OmniStemWorker.exe"),
    (Join-Path $installDir "release-manifest.json"),
    (Join-Path $installDir "unins000.exe")
)
foreach ($file in $required) {
    if (-not (Test-Path $file)) {
        throw "Deployment validation failed; missing $file"
    }
}
