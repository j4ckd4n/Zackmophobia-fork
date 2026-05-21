param(
    [Parameter(Mandatory = $false)]
    [string]$GameExe = "C:\Program Files (x86)\Steam\steamapps\common\Phasmophobia\Phasmophobia.exe",

    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [Parameter(Mandatory = $false)]
    [switch]$SkipBuild,

    [Parameter(Mandatory = $false)]
    [string[]]$GameArgs = @()
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$solutionPath = Join-Path $repoRoot "TryAgainHook.sln"
$launcherPath = Join-Path $repoRoot "x64\$Configuration\LaunchWithDll.exe"
$builtDllPath = Join-Path $repoRoot "x64\$Configuration\Zackmophobia.dll"
$shadowDir = Join-Path $repoRoot "x64\Injected"

Write-Host "[INFO] Repo: $repoRoot"
Write-Host "[INFO] Config: $Configuration"

if (-not (Test-Path $GameExe)) {
    throw "Game executable not found: $GameExe"
}

if (-not $SkipBuild) {
    Write-Host "[INFO] Building solution ($Configuration|x64)..."
    & msbuild $solutionPath /p:Configuration=$Configuration /p:Platform=x64 /m
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path $launcherPath)) {
    throw "Launch helper not found: $launcherPath`nBuild the LaunchWithDll project first."
}

if (-not (Test-Path $builtDllPath)) {
    throw "Built DLL not found: $builtDllPath"
}

New-Item -ItemType Directory -Path $shadowDir -Force | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss_fff"
$shadowDll = Join-Path $shadowDir "Zackmophobia_$stamp.dll"

# Use a unique shadow copy so the main build output can be rebuilt immediately.
Copy-Item -Path $builtDllPath -Destination $shadowDll -Force

Write-Host "[INFO] Launching game with injected shadow DLL:"
Write-Host "       $shadowDll"

$argList = @($GameExe, $shadowDll)
if ($GameArgs.Count -gt 0) {
    $argList += $GameArgs
}

& $launcherPath @argList
if ($LASTEXITCODE -ne 0) {
    throw "LaunchWithDll failed with exit code $LASTEXITCODE"
}

Write-Host "[OK] Injection launch complete."
Write-Host "[TIP] Press END in-game to unload before rebuilding/reinjecting."
