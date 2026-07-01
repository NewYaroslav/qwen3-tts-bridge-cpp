param(
    [Parameter(Mandatory = $true)]
    [string]$ModelPath,
    [string]$Python = "py",
    [string[]]$PythonArgs,
    [switch]$UseVenv,
    [string]$VenvPath = ".venv-packaging",
    [string]$WorkerExe = "dist/QwenTTSBridge/worker/qwen_tts_worker.exe",
    [int]$TimeoutSeconds = 600,
    [string]$Device = "cuda",
    [string]$Dtype = "auto",
    [string]$AttnImplementation = "",
    [string]$Text = "Packaged Qwen worker smoke test.",
    [string]$Language = "auto",
    [string]$Speaker = "",
    [string]$Instruction = ""
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

function Assert-PackagingPythonVersion {
    $VersionOutput = & $Python @PythonArgs -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to run packaging test Python. Install Python $RequiredPythonVersion or pass -Python/-PythonArgs explicitly."
    }

    $Version = ($VersionOutput | Select-Object -First 1).Trim()
    if ($Version -ne $RequiredPythonVersion) {
        throw "Packaging test Python must be $RequiredPythonVersion; selected Python is $Version. Recreate $VenvPath with Python $RequiredPythonVersion or pass -Python/-PythonArgs explicitly."
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

Assert-PackagingPythonVersion

$WorkerSrc = Resolve-RepoPath "worker/src"
if ([string]::IsNullOrWhiteSpace($env:PYTHONPATH)) {
    $env:PYTHONPATH = $WorkerSrc
}
else {
    $env:PYTHONPATH = "$WorkerSrc$([IO.Path]::PathSeparator)$env:PYTHONPATH"
}

$Arguments = @(
    "tests/python/verify_packaged_worker.py",
    (Resolve-RepoPath $WorkerExe),
    "--engine",
    "qwen",
    "--model-path",
    (Resolve-RepoPath $ModelPath),
    "--device",
    $Device,
    "--dtype",
    $Dtype,
    "--timeout-seconds",
    "$TimeoutSeconds",
    "--text",
    $Text,
    "--language",
    $Language
)

if (-not [string]::IsNullOrWhiteSpace($AttnImplementation)) {
    $Arguments += @("--attn-implementation", $AttnImplementation)
}
if (-not [string]::IsNullOrWhiteSpace($Speaker)) {
    $Arguments += @("--speaker", $Speaker)
}
if (-not [string]::IsNullOrWhiteSpace($Instruction)) {
    $Arguments += @("--instruction", $Instruction)
}

Invoke-ProjectPython $Arguments
