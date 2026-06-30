param(
    [string]$Python = "py",
    [string[]]$PythonArgs = @("-3")
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

function Invoke-ProjectPython {
    param(
        [string[]]$Arguments
    )

    & $Python @PythonArgs @Arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Invoke-ProjectPython @(
    "-m",
    "pip",
    "--disable-pip-version-check",
    "install",
    "--no-warn-script-location",
    "-r",
    "worker/requirements-dev.lock.txt",
    "-e",
    "worker"
)
