# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Theo H.
[CmdletBinding()]
param(
    [ValidateRange(1, 32)] [int] $N,
    [string] $Destination = (Join-Path $PSScriptRoot '../build/unpacked'),
    [string] $Bash
)
$ErrorActionPreference = 'Stop'
if (-not $Bash) {
    $bashCandidates = @(
        'C:/msys64/usr/bin/bash.exe',
        (Join-Path ${env:ProgramFiles} 'Git/bin/bash.exe')
    )
    $Bash = $bashCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $Bash) { $Bash = (Get-Command bash -ErrorAction Stop).Source }
}
$selection = if ($PSBoundParameters.ContainsKey('N')) { [string] $N } else { 'all' }
$scriptPath = (Join-Path $PSScriptRoot 'unpack-corpus.sh').Replace('\', '/')
$destinationPath = [System.IO.Path]::GetFullPath($Destination).Replace('\', '/')
& $Bash $scriptPath $selection $destinationPath
if ($LASTEXITCODE -ne 0) { throw "Corpus unpacking failed (exit $LASTEXITCODE)." }
