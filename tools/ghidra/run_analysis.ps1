# Ghidra headless analysis driver for Wind Waker main.dol.
#
# Usage:
#   ./tools/ghidra/run_analysis.ps1                    # first run: import + analyze + export
#   ./tools/ghidra/run_analysis.ps1 -ExportOnly        # subsequent runs: re-export only
#   ./tools/ghidra/run_analysis.ps1 -SkipDecompile     # skip the slow decompile pass
#   ./tools/ghidra/run_analysis.ps1 -Reanalyze         # nuke project + re-import (clean slate)
#
# Outputs JSONs to tools/ghidra/out/.

param(
    [switch]$ExportOnly,
    [switch]$SkipDecompile,
    [switch]$Reanalyze,
    [string]$GhidraDir = "D:\tools\ghidra_12.1.1_PUBLIC",
    [string]$Binary = "D:\recomp\gc\ww\main.elf"
)

$ErrorActionPreference = "Stop"

$RepoRoot   = "D:\recomp\gc\ww"
$ScriptDir  = "$RepoRoot\tools\ghidra\scripts"
$OutDir     = "$RepoRoot\tools\ghidra\out"
$ProjectDir = "$RepoRoot\tools\ghidra\project"
$ProjectName = "ww_dol"

$Headless = Join-Path $GhidraDir "support\analyzeHeadless.bat"
if (-not (Test-Path $Headless)) {
    Write-Error "analyzeHeadless not found at $Headless"
}

# Ghidra has no stock DOL loader. tools/ghidra/dol_to_elf.py wraps each DOL
# section as a PROGBITS LOAD segment in a big-endian PPC32 ELF, which the
# stock ELF loader handles natively at the correct VAs. Regenerate if the
# DOL has been updated since the ELF was last written.
$DolPath = "D:\recomp\gc\ww\main.dol"
$Converter = "D:\recomp\gc\ww\tools\ghidra\dol_to_elf.py"
if (-not (Test-Path $Binary) -or
    (Get-Item $DolPath).LastWriteTime -gt (Get-Item $Binary).LastWriteTime) {
    Write-Host "[ww-ghidra] Rebuilding $Binary from $DolPath"
    & python $Converter $DolPath $Binary
    if ($LASTEXITCODE -ne 0) {
        Write-Error "dol_to_elf failed"
    }
}

if ($Reanalyze -and (Test-Path $ProjectDir)) {
    Write-Host "[ww-ghidra] Removing existing project $ProjectDir"
    Remove-Item -Recurse -Force $ProjectDir
}

# analyzeHeadless errors out with "Directory not found" if the project parent
# is missing — must create the project dir even on a fresh import.
New-Item -ItemType Directory -Force -Path $ProjectDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$env:WW_GHIDRA_OUT = $OutDir

# Surface OutDir to scripts via env so they don't need to recompute paths.
$env:WW_GHIDRA_OUT = $OutDir

# Build the script list.
$scripts = @("ExportFunctions.java", "ExportSymbols.java")
if (-not $SkipDecompile) {
    $scripts += "ExportDecompiled.java"
}

$scriptArgs = @()
foreach ($s in $scripts) {
    $scriptArgs += "-postScript"
    $scriptArgs += $s
}

$projectFile = Join-Path $ProjectDir "$ProjectName.gpr"
$importedProgram = "$ProjectName/main.dol"

if ($ExportOnly -and (Test-Path $projectFile)) {
    Write-Host "[ww-ghidra] Running export scripts against existing project"
    & $Headless $ProjectDir $ProjectName `
        -process "main.dol" `
        -scriptPath $ScriptDir `
        $scriptArgs `
        -readOnly
} else {
    Write-Host "[ww-ghidra] Importing + analyzing $Binary"
    Write-Host "[ww-ghidra] Project: $ProjectDir\$ProjectName"
    Write-Host "[ww-ghidra] Out:     $OutDir"
    Write-Host "[ww-ghidra] Scripts: $($scripts -join ', ')"
    Write-Host ""

    # Stock Ghidra ships only PowerPC:BE:32:default for PPC32 — the DOL loader
    # uses that and runs fine on Gekko code (paired singles etc. won't decompile
    # cleanly but the function discovery / xrefs are what we need).
    & $Headless $ProjectDir $ProjectName `
        -import $Binary `
        -scriptPath $ScriptDir `
        $scriptArgs `
        -overwrite
}

if ($LASTEXITCODE -ne 0) {
    Write-Error "Ghidra exited with code $LASTEXITCODE"
}

Write-Host ""
Write-Host "[ww-ghidra] Outputs:"
Get-ChildItem $OutDir -File | ForEach-Object {
    "{0,12:N0}  {1}" -f $_.Length, $_.Name
} | Write-Host
