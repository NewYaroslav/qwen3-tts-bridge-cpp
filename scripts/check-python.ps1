param(
    [string]$Python = "py",
    [string[]]$PythonArgs,
    [switch]$UseVenv,
    [string]$VenvPath = ".venv"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

if (-not $PSBoundParameters.ContainsKey("PythonArgs")) {
    if ($Python -eq "py") {
        $PythonArgs = @("-3")
    }
    else {
        $PythonArgs = @()
    }
}

function Resolve-VenvPython {
    param(
        [string]$Path
    )

    if ([IO.Path]::IsPathRooted($Path)) {
        $ResolvedVenvPath = $Path
    }
    else {
        $ResolvedVenvPath = Join-Path $RepoRoot $Path
    }

    return Join-Path $ResolvedVenvPath "Scripts/python.exe"
}

if ($UseVenv) {
    $VenvPython = Resolve-VenvPython $VenvPath
    if (-not (Test-Path -LiteralPath $VenvPython)) {
        $Message = "Python virtual environment was not found at $VenvPath. " +
            "Run scripts/setup-python-dev.ps1 -UseVenv first."
        throw $Message
    }

    $Python = $VenvPython
    $PythonArgs = @()
}

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
