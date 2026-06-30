param(
    [string]$Python = "py",
    [string[]]$PythonArgs,
    [switch]$UseVenv,
    [string]$VenvPath = ".venv-packaging",
    [string]$WorkerExe = "dist/QwenTTSBridge/worker/qwen_tts_worker.exe",
    [int]$TimeoutSeconds = 20,
    [int]$MockChunks = 1
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

function Resolve-RepoPath {
    param(
        [string]$Path
    )

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }

    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Resolve-VenvPython {
    param(
        [string]$Path
    )

    $ResolvedVenvPath = Resolve-RepoPath $Path
    return Join-Path $ResolvedVenvPath "Scripts/python.exe"
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

if ($UseVenv) {
    $VenvPython = Resolve-VenvPython $VenvPath
    if (-not (Test-Path -LiteralPath $VenvPython)) {
        $Message = "Python virtual environment was not found at $VenvPath. " +
            "Run scripts/setup-python-packaging.ps1 -UseVenv first."
        throw $Message
    }

    $Python = $VenvPython
    $PythonArgs = @()
}

$WorkerSrc = Resolve-RepoPath "worker/src"
if ([string]::IsNullOrWhiteSpace($env:PYTHONPATH)) {
    $env:PYTHONPATH = $WorkerSrc
}
else {
    $env:PYTHONPATH = "$WorkerSrc$([IO.Path]::PathSeparator)$env:PYTHONPATH"
}

Invoke-ProjectPython @(
    "tests/python/verify_packaged_worker.py",
    (Resolve-RepoPath $WorkerExe),
    "--timeout-seconds",
    "$TimeoutSeconds",
    "--mock-chunks",
    "$MockChunks"
)
