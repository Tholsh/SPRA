# SPDX-License-Identifier: GPL-3.0-or-later
# Prove output arrives while the producer still holds stdin open.
param(
    [Parameter(Mandatory=$true)][string]$Tool,
    [Parameter(Mandatory=$true)][string]$Codec,
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$Spra,
    [string]$Xz = 'xz'
)
$ErrorActionPreference='Stop'
function Start-PipeProcess($exe,$arguments) {
    $s=[Diagnostics.ProcessStartInfo]::new();$s.FileName=$exe;$s.Arguments=$arguments
    $s.UseShellExecute=$false;$s.CreateNoWindow=$true
    $s.RedirectStandardInput=$true;$s.RedirectStandardOutput=$true;$s.RedirectStandardError=$true
    $p=[Diagnostics.Process]::Start($s)
    return @{P=$p;Error=$p.StandardError.ReadToEndAsync()}
}
function Test-Live($commands,[byte[]]$inputBytes,[bool]$capture) {
    $processes=@();$links=@();$closed=@();$result=[IO.MemoryStream]::new()
    try {
        foreach($command in $commands){$processes+=Start-PipeProcess $command.Exe $command.Args}
        for($i=0;$i+1 -lt $processes.Count;$i++){$links+=$processes[$i].P.StandardOutput.BaseStream.CopyToAsync($processes[$i+1].P.StandardInput.BaseStream);$closed+=$false}
        $first=$processes[0].P;$last=$processes[-1].P
        $buffer=New-Object byte[] 64
        $read=$last.StandardOutput.BaseStream.ReadAsync($buffer,0,$buffer.Length)
        $prefix=$inputBytes.Length-32
        $feed=$first.StandardInput.BaseStream.WriteAsync($inputBytes,0,$prefix)
        $observed=0
        do {
            if(!$read.Wait(10000) -or $read.Result -le 0 -or $last.HasExited){throw 'No payload output before input EOF'}
            $observed+=$read.Result
            if($capture){$result.Write($buffer,0,$read.Result)}
            if($observed -lt 4096){$read=$last.StandardOutput.BaseStream.ReadAsync($buffer,0,$buffer.Length)}
        }while($observed -lt 4096)
        if($capture){$sink=$result}else{$sink=[IO.Stream]::Null}
        $drain=$last.StandardOutput.BaseStream.CopyToAsync($sink)
        if(!$feed.Wait(10000)){throw 'Producer stalled'}
        $first.StandardInput.BaseStream.Write($inputBytes,$prefix,32);$first.StandardInput.Close()
        $timer=[Diagnostics.Stopwatch]::StartNew()
        do {
            $done=$drain.IsCompleted
            for($i=0;$i -lt $links.Count;$i++){
                if(!$closed[$i] -and $links[$i].IsCompleted){[void]$links[$i].GetAwaiter().GetResult();$processes[$i+1].P.StandardInput.Close();$closed[$i]=$true}
                if(!$links[$i].IsCompleted){$done=$false}
            }
            if($timer.Elapsed.TotalSeconds -gt 60){throw 'Pipeline completion timeout'}
            if(!$done){[Threading.Thread]::Sleep(1)}
        }while(!$done)
        [void]$drain.GetAwaiter().GetResult()
        foreach($entry in $processes){$entry.P.WaitForExit();$errorText=$entry.Error.GetAwaiter().GetResult();if($entry.P.ExitCode -ne 0){throw $errorText}}
        return ,$result.ToArray()
    }finally{foreach($entry in $processes){if(!$entry.P.HasExited){$entry.P.Kill()};$entry.P.Dispose()};$result.Dispose()}
}
$bytes=[IO.File]::ReadAllBytes($Spra)
$encoded=Test-Live @(@{Exe=$Tool;Args='encode'}) $bytes $true
$decoded=Test-Live @(@{Exe=$Tool;Args='decode'}) $encoded $true
$liveXz=Test-Live @(@{Exe=$Xz;Args='-c -T1 --block-size=256KiB --lzma2=preset=9e,dict=256MiB,lc=0,lp=0,pb=0'}) $encoded $true
$liveXzDecoded=Test-Live @(@{Exe=$Xz;Args='-dc'}) $liveXz $true
$sha=[Security.Cryptography.SHA256]::Create()
try {
    if([BitConverter]::ToString($sha.ComputeHash($bytes)) -ne [BitConverter]::ToString($sha.ComputeHash($decoded))){throw 'Live SPBE round trip mismatch'}
    if([BitConverter]::ToString($sha.ComputeHash($encoded)) -ne [BitConverter]::ToString($sha.ComputeHash($liveXzDecoded))){throw 'Live xz round trip mismatch'}
}finally{$sha.Dispose()}
$compressed=[IO.File]::ReadAllBytes($Archive)
[void](Test-Live @(@{Exe=$Xz;Args='-dc'},@{Exe=$Tool;Args='decode'},@{Exe=$Codec;Args='decode'}) $compressed $false)
Write-Output 'Live encode, live decode, and .bpe.xz -> raw word all produced payload before input EOF.'
Write-Output 'xz compression with 256KiB blocks also produced payload before EOF and round-tripped exactly.'
