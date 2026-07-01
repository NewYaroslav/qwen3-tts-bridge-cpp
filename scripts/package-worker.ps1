param(
    [string]$Python = "py",
    [string[]]$PythonArgs,
    [switch]$UseVenv,
    [string]$VenvPath = ".venv-packaging",
    [string]$OutputRoot = "dist/QwenTTSBridge",
    [string]$NuitkaWorkRoot = "tmp/nuitka-worker",
    [switch]$Clean,
    [switch]$DryRun,
    [switch]$AssumeYesForDownloads,
    [switch]$IncludeQwenPackage,
    [ValidateSet("None", "CustomVoice", "VoiceDesign", "VoiceClone", "Full")]
    [string]$QwenProfile = "None",
    [string]$NuitkaReportPath = "",
    [switch]$ShowNuitkaProgress,
    [switch]$ShowNuitkaMemory,
    [switch]$StrictBloatChecks,
    [string[]]$ExtraNuitkaOptions = @()
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

function Assert-UnderRepo {
    param(
        [string]$Path
    )

    $ResolvedRepoRoot = [IO.Path]::GetFullPath($RepoRoot)
    $ResolvedPath = [IO.Path]::GetFullPath($Path)
    $RepoPrefix = $ResolvedRepoRoot.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    if (
        $ResolvedPath -ne $ResolvedRepoRoot -and
        -not $ResolvedPath.StartsWith(
            $RepoPrefix,
            [StringComparison]::OrdinalIgnoreCase
        )
    ) {
        throw "Path must be inside the repository: $ResolvedPath"
    }
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
        throw "Unable to run packaging Python. Install Python $RequiredPythonVersion or pass -Python/-PythonArgs explicitly."
    }

    $Version = ($VersionOutput | Select-Object -First 1).Trim()
    if ($Version -ne $RequiredPythonVersion) {
        throw "Packaging Python must be $RequiredPythonVersion; selected Python is $Version. Recreate $VenvPath with Python $RequiredPythonVersion or pass -Python/-PythonArgs explicitly."
    }
}

function Format-CommandLine {
    param(
        [string]$Executable,
        [string[]]$Arguments
    )

    $Items = @($Executable) + $Arguments
    return ($Items | ForEach-Object {
        if ($_ -match "[\s`"]") {
            return '"' + ($_ -replace '"', '\"') + '"'
        }
        return $_
    }) -join " "
}

function Get-QwenBaseNuitkaOptions {
    $QwenNuitkaConfig = Resolve-RepoPath "worker/packaging/nuitka-qwen-runtime.yml"

    return @(
        # Include only the runtime Qwen modules used by the bridge worker.
        # A broad --include-package=qwen_tts also pulls qwen_tts.cli/demo UI
        # code and encourages Nuitka to inspect much more of Transformers.
        "--user-package-configuration-file=$QwenNuitkaConfig",
        "--include-module=qwen_tts",
        "--include-package=qwen_tts.inference",
        "--include-module=qwen_tts.core",
        "--include-module=qwen_tts.core.models",
        "--include-module=qwen_tts.core.models.configuration_qwen3_tts",
        "--include-module=qwen_tts.core.models.modeling_qwen3_tts",
        "--include-module=qwen_tts.core.models.processing_qwen3_tts",
        "--include-module=qwen_tts.core.tokenizer_12hz.configuration_qwen3_tts_tokenizer_v2",
        "--include-module=qwen_tts.core.tokenizer_12hz.modeling_qwen3_tts_tokenizer_v2",
        "--include-module=qwen_tts.core.tokenizer_12hz.optimized_decoder",
        "--include-module=qwen_tts.core.tokenizer_25hz.configuration_qwen3_tts_tokenizer_v1",
        "--include-module=qwen_tts.core.tokenizer_25hz.modeling_qwen3_tts_tokenizer_v1",
        "--include-package-data=qwen_tts",
        "--nofollow-import-to=qwen_tts.cli",
        "--nofollow-import-to=gradio",
        "--nofollow-import-to=einops.layers.flax",
        "--nofollow-import-to=einops.layers.keras",
        "--nofollow-import-to=einops.layers.oneflow",
        "--nofollow-import-to=einops.layers.paddle",
        "--nofollow-import-to=einops.layers.tensorflow",
        "--nofollow-import-to=torch._dynamo",
        "--nofollow-import-to=torch._functorch",
        "--nofollow-import-to=torch._inductor",
        "--nofollow-import-to=torch.fx.experimental.symbolic_shapes",
        "--nofollow-import-to=torch.utils._sympy",
        "--nofollow-import-to=torch.testing._internal",
        "--nofollow-import-to=functorch",
        "--noinclude-setuptools-mode=nofollow",
        "--noinclude-pytest-mode=nofollow",
        "--noinclude-IPython-mode=nofollow",
        "--noinclude-dask-mode=nofollow",
        "--noinclude-numba-mode=nofollow",
        "--module-parameter=torch-disable-jit=yes",
        "--module-parameter=numba-disable-jit=yes",
        "--disable-plugins=transformers"
    )
}

function Get-QwenVoiceCloneNuitkaOptions {
    return @(
        "--include-package=librosa",
        "--include-module=soundfile"
    )
}

function Get-QwenFullNuitkaOptions {
    return @(
        "--include-package=qwen_tts",
        "--include-package-data=qwen_tts"
    )
}

function Get-QwenProfileNuitkaOptions {
    param(
        [string]$Profile
    )

    switch ($Profile) {
        "None" {
            return @()
        }
        "CustomVoice" {
            return Get-QwenBaseNuitkaOptions
        }
        "VoiceDesign" {
            return Get-QwenBaseNuitkaOptions
        }
        "VoiceClone" {
            return (Get-QwenBaseNuitkaOptions) + (Get-QwenVoiceCloneNuitkaOptions)
        }
        "Full" {
            return Get-QwenFullNuitkaOptions
        }
    }

    throw "Unsupported QwenProfile: $Profile"
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

$PackageRoot = Resolve-RepoPath $OutputRoot
$NuitkaOutputRoot = Resolve-RepoPath $NuitkaWorkRoot
$EntryPoint = Resolve-RepoPath "worker/packaging/qwen_tts_worker_entry.py"
$WorkerSrc = Resolve-RepoPath "worker/src"
$NuitkaReport = $null

if (-not [string]::IsNullOrWhiteSpace($NuitkaReportPath)) {
    $NuitkaReport = Resolve-RepoPath $NuitkaReportPath
}

Assert-UnderRepo $PackageRoot
Assert-UnderRepo $NuitkaOutputRoot
Assert-UnderRepo $EntryPoint
Assert-UnderRepo $WorkerSrc
if ($null -ne $NuitkaReport) {
    Assert-UnderRepo $NuitkaReport
}

if ([string]::IsNullOrWhiteSpace($env:PYTHONPATH)) {
    $env:PYTHONPATH = $WorkerSrc
}
else {
    $env:PYTHONPATH = "$WorkerSrc$([IO.Path]::PathSeparator)$env:PYTHONPATH"
}

$NuitkaArgs = @(
    "-m",
    "nuitka",
    "--mode=standalone",
    "--output-dir=$NuitkaOutputRoot",
    "--output-filename=qwen_tts_worker.exe",
    "--include-package=qwen_tts_bridge_worker",
    "--remove-output"
)

if ($AssumeYesForDownloads) {
    $NuitkaArgs += "--assume-yes-for-downloads"
}

if ($IncludeQwenPackage) {
    if ($QwenProfile -ne "None") {
        throw "-IncludeQwenPackage cannot be combined with -QwenProfile $QwenProfile."
    }
    $QwenProfile = "CustomVoice"
}

$NuitkaArgs += Get-QwenProfileNuitkaOptions $QwenProfile

if ($StrictBloatChecks) {
    $NuitkaArgs += "--noinclude-default-mode=error"
}

if ($null -ne $NuitkaReport) {
    $NuitkaArgs += "--report=$NuitkaReport"
}

if ($ShowNuitkaProgress) {
    $NuitkaArgs += "--show-progress"
}

if ($ShowNuitkaMemory) {
    $NuitkaArgs += "--show-memory"
}

$NuitkaArgs += $ExtraNuitkaOptions
$NuitkaArgs += $EntryPoint

Write-Host "Nuitka command:"
Write-Host (Format-CommandLine $Python (@($PythonArgs) + $NuitkaArgs))
Write-Host "Package output: $PackageRoot"

if ($DryRun) {
    return
}

if ($Clean) {
    foreach ($Path in @($PackageRoot, $NuitkaOutputRoot)) {
        Assert-UnderRepo $Path
        if (Test-Path -LiteralPath $Path) {
            Remove-Item -LiteralPath $Path -Recurse -Force
        }
    }
}

if ($null -ne $NuitkaReport) {
    $NuitkaReportParent = Split-Path -Parent $NuitkaReport
    if (-not [string]::IsNullOrWhiteSpace($NuitkaReportParent)) {
        New-Item -ItemType Directory -Force -Path $NuitkaReportParent | Out-Null
    }
}

Invoke-ProjectPython $NuitkaArgs

$ExpectedNuitkaDist = Join-Path $NuitkaOutputRoot "qwen_tts_worker_entry.dist"
if (Test-Path -LiteralPath $ExpectedNuitkaDist) {
    $NuitkaDist = $ExpectedNuitkaDist
}
else {
    $DistCandidates = @(
        Get-ChildItem -LiteralPath $NuitkaOutputRoot -Directory -Filter "*.dist"
    )
    if ($DistCandidates.Count -ne 1) {
        throw "Expected one Nuitka .dist directory under $NuitkaOutputRoot."
    }
    $NuitkaDist = $DistCandidates[0].FullName
}

$WorkerOutput = Join-Path $PackageRoot "worker"
if (Test-Path -LiteralPath $WorkerOutput) {
    Assert-UnderRepo $WorkerOutput
    Remove-Item -LiteralPath $WorkerOutput -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $WorkerOutput | Out-Null
Copy-Item -Path (Join-Path $NuitkaDist "*") -Destination $WorkerOutput -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "config") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "models") | Out-Null

$WorkerExe = Join-Path $WorkerOutput "qwen_tts_worker.exe"
if (-not (Test-Path -LiteralPath $WorkerExe)) {
    throw "Packaged worker executable was not found: $WorkerExe"
}

Write-Host "Packaged worker: $WorkerExe"
