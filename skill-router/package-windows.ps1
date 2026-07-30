#Requires -Version 5.1
[CmdletBinding()]
param(
    [string] $OutputRoot = (Join-Path $PSScriptRoot "dist")
)

$ErrorActionPreference = "Stop"
$package = Get-Content (Join-Path $PSScriptRoot "package.json") -Raw | ConvertFrom-Json
$version = [string]$package.version
if ($version -notmatch '^[0-9A-Za-z][0-9A-Za-z._-]*$') {
    throw "package.json version must be a single safe filename segment."
}
$releaseName = "skill-router-windows-x64-$version"
$stage = Join-Path $OutputRoot $releaseName
$zip = Join-Path $OutputRoot "$releaseName.zip"
$work = Join-Path $OutputRoot ".package-work-$releaseName"

$vcvarsCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"),
    (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"),
    (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"),
    (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat")
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vcvars) { throw "Visual Studio 2022 C++ Build Tools (x64) were not found." }

$resolvedRoot = [IO.Path]::GetFullPath($OutputRoot)
$resolvedStage = [IO.Path]::GetFullPath($stage)
$resolvedWork = [IO.Path]::GetFullPath($work)
$rootPrefix = $resolvedRoot.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
) + [IO.Path]::DirectorySeparatorChar
$resolvedZip = [IO.Path]::GetFullPath($zip)

if (-not $resolvedStage.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not $resolvedWork.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not $resolvedZip.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to package outside the requested output root."
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
foreach ($path in @($stage, $work)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $path | Out-Null
}

try {
    foreach ($item in @(
        "main.cpp",
        "skill_library.hpp",
        "mcp_server.hpp",
        "test_library.cpp",
        "skilllib_c.h",
        "skilllib_c.cpp",
        "test_c_api.cpp",
        "build_windows_msvc.bat"
    )) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $item) -Destination $work
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "third_party") -Destination $work -Recurse

    Push-Location $work
    try {
        $buildCommand = 'call "' + $vcvars + '" && call build_windows_msvc.bat'
        & cmd.exe /d /s /c $buildCommand
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE."
        }
        & (Join-Path $work "build\test_library.exe")
        if ($LASTEXITCODE -ne 0) {
            throw "C++ tests failed with exit code $LASTEXITCODE."
        }
        & (Join-Path $work "build\test_c_api.exe")
        if ($LASTEXITCODE -ne 0) {
            throw "C ABI tests failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }

    foreach ($item in @(
        "package.json",
        "LICENSE",
        "README.md",
        "WINDOWS_README.md",
        "INTEGRATION_MANUAL.md",
        "index-skills.ps1"
    )) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $item) -Destination $stage
    }

    Copy-Item -LiteralPath (Join-Path $work "skillrouter.exe") -Destination $stage
    Copy-Item -LiteralPath (Join-Path $work "skilllib_c.h") -Destination $stage
    Copy-Item -LiteralPath (Join-Path $work "build\skillrouter_c.dll") -Destination $stage
    Copy-Item -LiteralPath (Join-Path $work "build\skillrouter_c.lib") -Destination $stage

    $stageDocs = Join-Path $stage "docs"
    New-Item -ItemType Directory -Path $stageDocs | Out-Null
    foreach ($doc in @(
        "RANKING_AND_IDENTITY_CONTRACT.md",
        "ARCHITECTURE_MEASUREMENT_LEDGER.md",
        "INTEGRATION_PATTERNS.md",
        "C_ABI.md"
    )) {
        Copy-Item -LiteralPath (Join-Path (Join-Path $PSScriptRoot "..\docs") $doc) `
            -Destination $stageDocs
    }

    New-Item -ItemType Directory -Path (Join-Path $stage "skills") | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "skills\skill_router") `
        -Destination (Join-Path $stage "skills\skill_router") -Recurse
    New-Item -ItemType Directory -Path (Join-Path $stage "skill_library") | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "shipping\SKILL_LIBRARY_README.md") `
        -Destination (Join-Path $stage "skill_library\README.md")

    $manifest = Get-ChildItem -LiteralPath $stage -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($stage.Length + 1).Replace("\", "/")
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $relative"
        }
    [IO.File]::WriteAllLines(
        (Join-Path $stage "SHA256SUMS.txt"),
        $manifest,
        [Text.UTF8Encoding]::new($false)
    )

    if (Test-Path -LiteralPath $zip) {
        Remove-Item -LiteralPath $zip -Force
    }
    Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal

    Write-Host "Release folder: $stage"
    Write-Host "Release ZIP:    $zip"
    Write-Host "ZIP SHA-256:    $((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)"
} finally {
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force
    }
}
