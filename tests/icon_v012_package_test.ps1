#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
param([Parameter(Mandatory)][string]$Profile,[Parameter(Mandatory)][string]$Zip,
    [Parameter(Mandatory)][string]$EvidenceRoot,[Parameter(Mandatory)][string]$DependencyRoot,
    [string]$Root=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
Import-Module (Join-Path $Root packaging/SecBR2Package.psm1) -DisableNameChecking
Import-Module (Join-Path $Root packaging/IconV012Package.psm1) -DisableNameChecking
$profilePath=(Resolve-Path -LiteralPath $Profile).Path
$p=Get-Content -LiteralPath $profilePath -Raw|ConvertFrom-Json -AsHashtable
$archive=Read-BR2Archive (Resolve-Path -LiteralPath $Zip).Path $p
$run=Join-Path ([IO.Path]::GetFullPath($EvidenceRoot)) ('zip-tests-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $run
$results=[Collections.Generic.List[object]]::new()
function Case($Name,$Expected,$Action) {
    $actual='ACCEPT'
    try{& $Action|Out-Null}catch{$actual=$_.Exception.Message}
    $ok=if($Expected){$actual.Contains($Expected,[StringComparison]::Ordinal)}else{$actual -ceq 'ACCEPT'}
    $results.Add([ordered]@{name=$Name;result=$(if($ok){'PASS'}else{'FAIL'});actual=$actual})
}
function New-ZipFixture($Mode) {
    $file=Join-Path $run "$Mode.zip"
    $output=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $zipArchive=[IO.Compression.ZipArchive]::new($output,[IO.Compression.ZipArchiveMode]::Create,$true)
    try {
        $items=@($p.files)
        if($Mode -eq 'missing'){$items=$items[0..($items.Count-2)]}
        if($Mode -eq 'extra'){$items+=@{path='extra.txt'}}
        for($i=0;$i -lt $items.Count;++$i) {
            $name=$items[$i].path
            [byte[]]$data=if($name -eq 'extra.txt'){[Text.Encoding]::UTF8.GetBytes('extra')}else{[byte[]]$archive.contents[$name].Clone()}
            if($Mode -eq 'duplicate' -and $i -eq 1){$name=$items[0].path}
            if($Mode -eq 'traversal' -and $name -eq 'README.md'){$name='../escape.txt'}
            if($Mode -eq 'reserved' -and $name -eq 'README.md'){$name='CON'}
            if($Mode -eq 'changed-content' -and $name -eq 'README.md'){$data[0]=$data[0] -bxor 1}
            if($Mode -eq 'oversized' -and $name -eq 'README.md'){$data=[byte[]](@($data)+@(0))}
            $entry=$zipArchive.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime=[DateTimeOffset]::Parse($p.zipTimestamp)
            if($Mode -eq 'timestamp' -and $i -eq 0){$entry.LastWriteTime=$entry.LastWriteTime.AddSeconds(2)}
            $entry.ExternalAttributes=if($Mode -eq 'symlink' -and $i -eq 0){-1577123840}else{32}
            $stream=$entry.Open();try{$stream.Write($data,0,$data.Length)}finally{$stream.Dispose()}
        }
    }finally{$zipArchive.Dispose();$output.Dispose()}
    return $file
}
Case 'adopted candidate archive accepted' '' {Read-BR2Archive (Resolve-Path -LiteralPath $Zip).Path $p}
$expectedCodes=@{missing='ZipLayout';extra='ZipLayout';duplicate='ZipFiles';traversal='RelativePath';reserved='RelativePath';'changed-content'='ZipHash';oversized='ZipSize';timestamp='ZipMetadata';symlink='ZipMetadata'}
foreach($mode in @('missing','extra','duplicate','traversal','reserved','changed-content','oversized','timestamp','symlink')){
    Case "$mode rejected" $expectedCodes[$mode] {Read-BR2Archive (New-ZipFixture $mode) $p}
}
Case 'trailing bytes rejected' 'ZipLayout' {
    $originalBytes=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Zip).Path)
    $bytes=[byte[]]::new($originalBytes.Length+1);[Array]::Copy($originalBytes,$bytes,$originalBytes.Length)
    $file=Join-Path $run trailing.zip;[IO.File]::WriteAllBytes($file,$bytes);Read-BR2Archive $file $p
}
foreach($kind in @('approval','run-traversal')){
    $expected=if($kind -eq 'approval'){'PackageApproval'}else{'PackageRunRoot'}
    Case "profile $kind rejected before product access" $expected {
        $view=Join-Path $run $kind
        $folder=Join-Path $view packaging/public
        $null=New-Item -ItemType Directory -Path $folder
        $copy=Get-Content -LiteralPath $profilePath -Raw|ConvertFrom-Json -AsHashtable
        if($kind -eq 'approval'){$copy.publicDistributionAllowed=$true}else{$copy.runRoot='../outside'}
        [IO.File]::WriteAllText((Join-Path $folder profile-icon-v012-rc1.json),($copy|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
        Read-IconPackageProfile $view $DependencyRoot
    }
}
[IO.File]::WriteAllText((Join-Path $run results.json),(ConvertTo-Json -InputObject @($results) -Depth 5).Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false))
$results|ConvertTo-Json -Depth 5
if(@($results|Where-Object result -CEQ FAIL).Count){throw 'ICON-012 package fixture tests failed'}
