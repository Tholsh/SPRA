# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.
# Convert a folder's .txt words while preserving its relative directory layout.
param(
    [Parameter(Mandatory = $true)][ValidateSet(0, 1)][int]$To,
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination,
    [ValidateRange(1, 13)][int]$N = 13,
    [switch]$DryRun,
    [switch]$Overwrite,
    [string]$Tool = (Join-Path $PSScriptRoot '../build/relabel-words.exe')
)
$ErrorActionPreference = 'Stop'
$toolArgs = @('--recursive', '--to', "$To", '-n', "$N")
if ($DryRun) { $toolArgs += '--dry-run' }
if ($Overwrite) { $toolArgs += '--overwrite' }
$toolArgs += @('--', $Source, $Destination)
& $Tool @toolArgs
exit $LASTEXITCODE
