#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Canonical archive/filesystem fixtures only. Never changes a real candidate.
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../tools/ShippingProfileSecBR1.ps1"
Import-Module "$PSScriptRoot/../packaging/SecBR2Package.psm1"
Import-Module "$PSScriptRoot/../tools/S1SourceContracts.psm1" -Global
$run=Join-Path $EvidenceRoot ('package-'+[guid]::NewGuid().ToString('N'))
New-S1OwnedDirectory $run
$utf8=[Text.UTF8Encoding]::new($false)
$results=[Collections.Generic.List[object]]::new()
function Case($Name,$Expected,$Body){
    $actual='ACCEPT'
    try{& $Body|Out-Null}catch{$actual=$_.Exception.Message}
    $ok=if($Expected){$actual.Contains($Expected,[StringComparison]::Ordinal)}else{$actual -ceq 'ACCEPT'}
    $results.Add([ordered]@{name=$Name;result=$(if($ok){'PASS'}else{'FAIL'});actual=$actual})
}
$payload=Join-Path $run payload
New-S1OwnedDirectory $payload
[void][IO.Directory]::CreateDirectory((Join-Path $payload licenses))
$files=@(foreach($relative in @('README.md','licenses/copyright')){
    $bytes=$utf8.GetBytes('public synthetic fixture: '+$relative)
    [IO.File]::WriteAllBytes((Join-Path $payload $relative),$bytes)
    [ordered]@{path=$relative;size=$bytes.Length;sha256=(Get-S1Hash $bytes)}
})
$profile=[ordered]@{files=$files;zipTimestamp='2026-10-30T00:00:00+09:00'}
$canonical=Join-Path $run canonical.zip
function New-ManualZip($Name,$Names,$Timestamp='2026-10-30T00:00:00+09:00'){
    $file=Join-Path $run ($Name+'.zip')
    $stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew)
    $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
    try{for($i=0;$i -lt $Names.Count;$i++){
        $entry=$zip.CreateEntry($Names[$i],[IO.Compression.CompressionLevel]::Optimal)
        $entry.ExternalAttributes=32;$entry.LastWriteTime=[DateTimeOffset]::Parse($Timestamp)
        $out=$entry.Open();$b=[IO.File]::ReadAllBytes((Join-Path $payload $profile.files[$i].path))
        try{$out.Write($b,0,$b.Length)}finally{$out.Dispose()}
    }}finally{$zip.Dispose();$stream.Dispose()}
    return $file
}
Case 'canonical writer and strict reader round trip' '' {
    Write-BR2Archive $canonical $payload $profile
    $read=Read-BR2Archive $canonical $profile
    if($read.contents.Count -ne 2){throw 'Wrong entry count'}
}
Case 'writer refuses existing archive without changing it' '' {
    $before=(Get-FileHash -LiteralPath $canonical).Hash
    $rejected=$false
    try{Write-BR2Archive $canonical $payload $profile}catch [IO.IOException]{$rejected=$true}
    if(!$rejected -or (Get-FileHash -LiteralPath $canonical).Hash -cne $before){throw 'Existing archive changed'}
}
Case 'trailing data rejected' 'ZipLayout' {
    $file=Join-Path $run trailing.zip
    [IO.File]::WriteAllBytes($file,[byte[]](@([IO.File]::ReadAllBytes($canonical))+@(0)))
    Read-BR2Archive $file $profile
}
Case 'preamble rejected' 'ZipLayout' {
    $file=Join-Path $run preamble.zip
    [IO.File]::WriteAllBytes($file,[byte[]](@(0)+@([IO.File]::ReadAllBytes($canonical))))
    Read-BR2Archive $file $profile
}
Case 'duplicate archive name rejected' 'ZipFiles' {
    Read-BR2Archive (New-ManualZip duplicate @('README.md','README.md')) $profile
}
Case 'traversal rejected before extraction' 'S1:' {
    Read-BR2Archive (New-ManualZip traversal @('../escape.txt','licenses/copyright')) $profile
}
Case 'unexpected timestamp rejected' 'ZipMetadata' {
    Read-BR2Archive (New-ManualZip date @('README.md','licenses/copyright') '2020-01-01T00:00:00+09:00') $profile
}
Case 'payload hash mismatch rejected' 'ZipHash' {
    $bad=[ordered]@{zipTimestamp=$profile.zipTimestamp;files=@(@{path=$files[0].path;size=$files[0].size;sha256=('0'*64)},$files[1])}
    Read-BR2Archive $canonical $bad
}
Case 'link-like external attributes rejected' 'ZipMetadata' {
    $b=[IO.File]::ReadAllBytes($canonical)
    $end=$b.Length-22;$central=[BitConverter]::ToInt32($b,$end+16)
    [BitConverter]::GetBytes([Convert]::ToUInt32('A1FF0000',16)).CopyTo($b,$central+38)
    $file=Join-Path $run link.zip
    [IO.File]::WriteAllBytes($file,$b)
    Read-BR2Archive $file $profile
}
Case 'unlisted settings file rejected' 'InputSet' {
    $tree=Join-Path $run extra
    [void][IO.Directory]::CreateDirectory((Join-Path $tree licenses))
    foreach($file in $files){[IO.File]::Copy((Join-Path $payload $file.path),(Join-Path $tree $file.path),$false)}
    [IO.File]::WriteAllText((Join-Path $tree settings.conf),'synthetic',$utf8)
    Assert-BR2Tree $tree $profile
}
Case 'unexpected empty directory rejected' 'DirectorySet' {
    $tree=Join-Path $run directory
    [void][IO.Directory]::CreateDirectory((Join-Path $tree licenses))
    foreach($file in $files){[IO.File]::Copy((Join-Path $payload $file.path),(Join-Path $tree $file.path),$false)}
    [void][IO.Directory]::CreateDirectory((Join-Path $tree foreign))
    Assert-BR2Tree $tree $profile
}
$summary=[ordered]@{result=$(if(@($results|Where-Object result -CEQ FAIL).Count){'FAIL'}else{'PASS'});kind='synthetic package/archive fixtures';cases=$results;candidateGenerated=$false;releaseApproved=$false}
Write-S1NewJson (Join-Path $run results.json) $summary
$summary|ConvertTo-Json -Depth 6
if($summary.result -ceq 'FAIL'){exit 1}
