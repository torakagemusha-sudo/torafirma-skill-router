#Requires -Version 5.1
<#
.SYNOPSIS
    Publish SKILL.md metadata and SHA-256 revisions into a Skill Router catalogue.

.DESCRIPTION
    Runs the 1.1.0 operator index over one or more source directories. Runtime
    telemetry is written to a database separate from the read-mostly catalogue.
    Existing 1.0.0 catalogues must be re-indexed before 1.1.0 consumers start.
#>

[CmdletBinding()]
param(
    [Parameter(Position=0, ValueFromPipeline)]
    [string[]] $SourcePaths = @((Join-Path $PSScriptRoot "skill_library")),

    [ValidateNotNullOrEmpty()]
    [string] $DatabasePath = (Join-Path $PSScriptRoot "skill_index.db"),

    [ValidateNotNullOrEmpty()]
    [string] $TelemetryDatabasePath = ((Join-Path $PSScriptRoot "skill_index.db") + ".telemetry.db"),

    [switch] $DryRun,

    [Parameter(ValueFromPipelineByPropertyName)]
    [string] $SkillRouterPath = (Join-Path $PSScriptRoot "skillrouter.exe")
)

begin {
    $ErrorActionPreference = "Stop"
    $skillrouterExe = Get-Command $SkillRouterPath -ErrorAction Stop | Select-Object -ExpandProperty Source

    Write-Host "Using skillrouter: $skillrouterExe" -ForegroundColor Cyan
    Write-Host "Catalogue:        $DatabasePath" -ForegroundColor Cyan
    Write-Host "Telemetry:        $TelemetryDatabasePath" -ForegroundColor Cyan

    & $skillrouterExe --help *> $null
    if ($LASTEXITCODE -ne 0) { throw "skillrouter exited with code $LASTEXITCODE" }

    $stats = @{ TotalScanned=0; Created=0; Updated=0; Unchanged=0; Errors=0 }
    $generations = New-Object System.Collections.Generic.List[string]
}

process {
    foreach ($sourcePath in $SourcePaths) {
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
            Write-Warning "Source path is not a directory: $sourcePath"
            $stats.Errors++
            continue
        }

        $skillFiles = Get-ChildItem -LiteralPath $sourcePath -Filter "SKILL.md" -Recurse -File -ErrorAction Stop |
            Sort-Object FullName
        $count = ($skillFiles | Measure-Object).Count
        $stats.TotalScanned += $count

        Write-Host "Scanning $sourcePath: $count SKILL.md file(s)" -ForegroundColor Yellow
        if ($count -eq 0) { continue }
        if ($DryRun) {
            $skillFiles | ForEach-Object { Write-Host "  [dry-run] $($_.FullName)" -ForegroundColor DarkGray }
            continue
        }

        $raw = & $skillrouterExe index $sourcePath `
            --db $DatabasePath `
            --telemetry-db $TelemetryDatabasePath `
            --role operator 2>&1 | Out-String

        if ($LASTEXITCODE -ne 0) {
            Write-Error "Index failed for $sourcePath`n$raw"
            $stats.Errors++
            continue
        }

        try {
            $result = $raw | ConvertFrom-Json
            $stats.Created += [int]$result.created
            $stats.Updated += [int]$result.updated
            $stats.Unchanged += [int]$result.unchanged
            if ([int]$result.errors -gt 0) { $stats.Errors += [int]$result.errors }
            if ($result.catalog_generation) { $generations.Add([string]$result.catalog_generation) }
            Write-Host "  created=$($result.created) updated=$($result.updated) unchanged=$($result.unchanged)" -ForegroundColor Green
            Write-Host "  catalogue generation: $($result.catalog_generation)" -ForegroundColor Green
        } catch {
            Write-Error "Index returned non-JSON output:`n$raw"
            $stats.Errors++
        }
    }
}

end {
    Write-Host ""
    Write-Host "Indexing complete" -ForegroundColor Cyan
    Write-Host "  scanned:   $($stats.TotalScanned)"
    Write-Host "  created:   $($stats.Created)" -ForegroundColor Green
    Write-Host "  updated:   $($stats.Updated)" -ForegroundColor Yellow
    Write-Host "  unchanged: $($stats.Unchanged)" -ForegroundColor Gray
    Write-Host "  errors:    $($stats.Errors)" -ForegroundColor $(if ($stats.Errors) { "Red" } else { "Green" })

    if (-not $DryRun -and $stats.Errors -eq 0) {
        Write-Host ""
        Write-Host "Consumer-readable state:" -ForegroundColor Cyan
        & $skillrouterExe stats `
            --db $DatabasePath `
            --telemetry-db $TelemetryDatabasePath `
            --role consumer
        if ($LASTEXITCODE -ne 0) { $stats.Errors++ }
    }

    if ($stats.Errors -gt 0) { exit 1 }
    exit 0
}
