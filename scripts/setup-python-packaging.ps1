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
$RequiredPythonVersion = "3.11"

if (-not $PSBoundParameters.ContainsKey("PythonArgs")) {
    if ($Python -eq "py") {
        $PythonArgs = @("-3.11")
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

function Assert-PackagingPythonVersion {
    param(
        [string]$Context
    )

    $VersionOutput = & $Python @PythonArgs -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to run $Context. Install Python $RequiredPythonVersion or pass -Python/-PythonArgs explicitly."
    }

    $Version = ($VersionOutput | Select-Object -First 1).Trim()
    if ($Version -ne $RequiredPythonVersion) {
        $Message = "$Context must use Python $RequiredPythonVersion; selected Python is $Version. "
        if ($UseVenv) {
            $Message += "Remove $VenvPath and rerun setup with Python $RequiredPythonVersion, or pass -Python/-PythonArgs explicitly."
        }
        else {
            $Message += "Pass -Python/-PythonArgs explicitly if needed."
        }
        throw $Message
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
        Assert-PackagingPythonVersion "Base Python for packaging venv"
        Invoke-ProjectPython @("-m", "venv", $VenvPath)
    }

    $Python = $VenvPython
    $PythonArgs = @()
}

Assert-PackagingPythonVersion "Packaging Python"

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
