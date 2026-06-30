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
    [string[]]$ExtraNuitkaOptions = @()
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

$PackageRoot = Resolve-RepoPath $OutputRoot
$NuitkaOutputRoot = Resolve-RepoPath $NuitkaWorkRoot
$EntryPoint = Resolve-RepoPath "worker/packaging/qwen_tts_worker_entry.py"
$WorkerSrc = Resolve-RepoPath "worker/src"

Assert-UnderRepo $PackageRoot
Assert-UnderRepo $NuitkaOutputRoot
Assert-UnderRepo $EntryPoint
Assert-UnderRepo $WorkerSrc

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
    $NuitkaArgs += "--include-package=qwen_tts"
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
