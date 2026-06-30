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

function Invoke-ProjectPython {
    param(
        [string[]]$Arguments
    )

    & $Python @PythonArgs @Arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
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
        Invoke-ProjectPython @("-m", "venv", $VenvPath)
    }

    $Python = $VenvPython
    $PythonArgs = @()
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
