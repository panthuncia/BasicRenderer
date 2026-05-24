[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$TargetDir,

    [Parameter(Position = 1)]
    [string]$BRNiflyExe = $(if ($env:BRNIFLY_EXE) { $env:BRNIFLY_EXE } else { "BRNifly.exe" }),

    [Parameter(Position = 2)]
    [string]$OutputDir = "."
)

$ErrorActionPreference = "Stop"

function Join-FlagNames {
    param([object[]]$Names)

    if ($null -eq $Names -or $Names.Count -eq 0) {
        return "<none>"
    }

    return ($Names -join ", ")
}

function Get-ShapeComboCount {
    param(
        [object]$Report,
        [string]$Flags1Hex,
        [string]$Flags2Hex
    )

    $count = 0
    foreach ($shape in @($Report.shapes)) {
        if ($shape.shader.shaderFlags1Hex -eq $Flags1Hex -and $shape.shader.shaderFlags2Hex -eq $Flags2Hex) {
            ++$count
        }
    }
    return $count
}

function Invoke-BRNiflyShaderFlags {
    param([string]$NifPath)

    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()
    try {
        $process = Start-Process `
            -FilePath $BRNiflyExe `
            -ArgumentList @("--shader-flags-json", $NifPath) `
            -NoNewWindow `
            -Wait `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath

        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = Get-Content -LiteralPath $stdoutPath -Raw
            Stderr = Get-Content -LiteralPath $stderrPath -Raw
        }
    }
    finally {
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    }
}

$targetPath = Resolve-Path -LiteralPath $TargetDir
if (-not (Test-Path -LiteralPath $targetPath -PathType Container)) {
    throw "Target directory does not exist: $TargetDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$outputPath = Resolve-Path -LiteralPath $OutputDir

$flagsByKey = @{}
$combinationsByKey = @{}
$rawReports = New-Object System.Collections.Generic.List[object]
$errors = New-Object System.Collections.Generic.List[object]

$nifPaths = Get-ChildItem -LiteralPath $targetPath -Recurse -File -Filter "*.nif" |
    Sort-Object FullName

foreach ($nif in $nifPaths) {
    try {
        $result = Invoke-BRNiflyShaderFlags -NifPath $nif.FullName
    }
    catch {
        throw "Failed to launch BRNifly: $($_.Exception.Message)"
    }

    if ($result.ExitCode -ne 0) {
        $errors.Add([pscustomobject]@{
            path = $nif.FullName
            error = $result.Stderr.Trim()
        })
        continue
    }

    try {
        $report = $result.Stdout | ConvertFrom-Json
    }
    catch {
        $errors.Add([pscustomobject]@{
            path = $nif.FullName
            error = "invalid JSON: $($_.Exception.Message)"
        })
        continue
    }

    $rawReports.Add($report)
    if ($report.status -ne "ok") {
        $message = if ($report.message) { $report.message } else { "unknown error" }
        $errors.Add([pscustomobject]@{
            path = $nif.FullName
            error = $message
        })
        continue
    }

    foreach ($flag in @($report.discoveredFlags)) {
        $key = "$($flag.set):$($flag.name)"
        $flagsByKey[$key] = $flag
    }

    foreach ($combo in @($report.uniqueCombinations)) {
        $key = "$($combo.shaderFlags1Hex)|$($combo.shaderFlags2Hex)"
        if (-not $combinationsByKey.ContainsKey($key)) {
            $combinationsByKey[$key] = [pscustomobject]@{
                shaderFlags1 = $combo.shaderFlags1
                shaderFlags1Hex = $combo.shaderFlags1Hex
                shaderFlags1Names = @($combo.shaderFlags1Names)
                shaderFlags2 = $combo.shaderFlags2
                shaderFlags2Hex = $combo.shaderFlags2Hex
                shaderFlags2Names = @($combo.shaderFlags2Names)
                files = New-Object System.Collections.Generic.HashSet[string]
                shapeCount = 0
            }
        }

        [void]$combinationsByKey[$key].files.Add($nif.FullName)
        $combinationsByKey[$key].shapeCount += Get-ShapeComboCount `
            -Report $report `
            -Flags1Hex $combo.shaderFlags1Hex `
            -Flags2Hex $combo.shaderFlags2Hex
    }
}

$flagsPath = Join-Path $outputPath "shader_flags.txt"
$combinationsPath = Join-Path $outputPath "shader_flag_combinations.txt"
$jsonlPath = Join-Path $outputPath "shader_flag_report.jsonl"

$flagLines = New-Object System.Collections.Generic.List[string]
$flagLines.Add("set`tname`tmask")
$flagsByKey.Values |
    Sort-Object @{ Expression = { if ($_.set -eq "shaderFlags1") { 0 } elseif ($_.set -eq "shaderFlags2") { 1 } else { 99 } } }, name |
    ForEach-Object {
        $flagLines.Add("$($_.set)`t$($_.name)`t$($_.maskHex)")
    }
Set-Content -LiteralPath $flagsPath -Value $flagLines -Encoding utf8

$combinationLines = New-Object System.Collections.Generic.List[string]
$combinationLines.Add("shaderFlags1`tshaderFlags2`tshaderFlags1Names`tshaderFlags2Names`tshapeCount`tfileCount")
$combinationsByKey.Values |
    Sort-Object shaderFlags1Hex, shaderFlags2Hex |
    ForEach-Object {
        $names1 = Join-FlagNames -Names $_.shaderFlags1Names
        $names2 = Join-FlagNames -Names $_.shaderFlags2Names
        $combinationLines.Add("$($_.shaderFlags1Hex)`t$($_.shaderFlags2Hex)`tshaderFlags1=[$names1]`tshaderFlags2=[$names2]`t$($_.shapeCount)`t$($_.files.Count)")
    }
Set-Content -LiteralPath $combinationsPath -Value $combinationLines -Encoding utf8

$jsonlLines = foreach ($report in $rawReports) {
    $report | ConvertTo-Json -Depth 100 -Compress
}
Set-Content -LiteralPath $jsonlPath -Value $jsonlLines -Encoding utf8

Write-Host "Scanned $($nifPaths.Count) .nif file(s)."
Write-Host "Wrote $flagsPath"
Write-Host "Wrote $combinationsPath"
Write-Host "Wrote $jsonlPath"

if ($errors.Count -gt 0) {
    $errorsPath = Join-Path $outputPath "shader_flag_errors.json"
    $errors | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $errorsPath -Encoding utf8
    Write-Warning "$($errors.Count) file(s) failed; details in $errorsPath"
}
