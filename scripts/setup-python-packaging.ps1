param(
    [string]$Python = "py",
    [string[]]$PythonArgs,
    [switch]$UseVenv,
    [string]$VenvPath = ".venv-packaging",
    [switch]$InstallQwenFork,
    [string]$QwenSourcePath = "external/python/Qwen3-TTS-streaming"
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

function Resolve-RepoPath {
    param(
        [string]$Path
    )

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }

    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
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
    "worker/requirements-packaging.lock.txt",
    "-e",
    "worker"
)

if ($InstallQwenFork) {
    $ResolvedQwenSourcePath = Resolve-RepoPath $QwenSourcePath
    $QwenPyProject = Join-Path $ResolvedQwenSourcePath "pyproject.toml"
    if (-not (Test-Path -LiteralPath $QwenPyProject)) {
        throw "Qwen source pyproject.toml was not found: $QwenPyProject"
    }

    Invoke-ProjectPython @(
        "-m",
        "pip",
        "--disable-pip-version-check",
        "install",
        "--no-warn-script-location",
        "-e",
        $ResolvedQwenSourcePath
    )
}
