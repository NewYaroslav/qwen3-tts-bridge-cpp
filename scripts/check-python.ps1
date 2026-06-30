param(
    [string]$Python = "py",
    [string[]]$PythonArgs = @("-3")
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$WorkerSrc = Join-Path $RepoRoot "worker/src"
if ([string]::IsNullOrWhiteSpace($env:PYTHONPATH)) {
    $env:PYTHONPATH = $WorkerSrc
}
else {
    $env:PYTHONPATH = "$WorkerSrc$([IO.Path]::PathSeparator)$env:PYTHONPATH"
}

function Invoke-ProjectPython {
    param(
        [string[]]$Arguments
    )

    & $Python @PythonArgs @Arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Invoke-ProjectPython @("-m", "ruff", "check", "worker/src", "tests/python")
Invoke-ProjectPython @("-m", "pyright")
Invoke-ProjectPython @("-m", "unittest", "discover", "-s", "tests/python")
