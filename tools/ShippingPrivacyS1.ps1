#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Neutral origin + unchanged versioned v2 postimages. No legacy dispatcher.
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot S1SourceContracts.psm1) -Force
function Stop-S1($Code) { throw [IO.InvalidDataException]::new('S1:'+ $Code) }
function Assert-S1Fields($Value,$Names) {
    if($Value -isnot [Collections.IDictionary]){Stop-S1 ObjectType}
    if($Value.Count -ne $Names.Count){Stop-S1 Fields}
    foreach($name in $Value.Keys){if($name -isnot [string] -or $name -cnotin $Names){Stop-S1 Fields}}
}
function Assert-S1Value($Value,$Expected,$Code) {
    Assert-S1String $Value
    if($Value -cne $Expected){Stop-S1 $Code}
}
function Get-S1Hash($Bytes) { return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)) }
function Assert-S1Selection($Profile,$Scope,$Privacy,$Configuration,$Architecture) {
    Assert-S1Value $Profile 'v2' Profile
    Assert-S1Value $Scope 's1-v1' Scope
    Assert-S1Value $Privacy 'ON' Privacy
    Assert-S1Value $Configuration 'Release' Configuration
    Assert-S1Value $Architecture 'x64' Architecture
}
function Read-S1Managed($Root) {
    $origin=Read-S1OriginContract $Root
    $specPath=Resolve-S1Path $Root 'cmake/privacy/managed-diagnostics-v2.json'
    $manifestPath=Resolve-S1Path $Root 'cmake/privacy/managed-sources-v2.tsv'
    Assert-S1Identity $specPath 2859 '4C83CDA89C029EFFF6EF88E3D33C2FEED57EB94B60882C4C1215CCD1BDA70E29'
    Assert-S1Identity $manifestPath 327881 '3D372FE265E962D845B8E6D375707071A30662A112B0F531928A006EDDF7F7F2'
    $spec=Read-S1StrictJson $specPath
    Assert-S1Fields $spec @('schema','id','pins','gitlinks','changes','noticeImplementationDate')
    if(($spec.schema -isnot [int] -and $spec.schema -isnot [long]) -or $spec.schema -ne 2){Stop-S1 ManagedSchema}
    Assert-S1Value $spec.id 'clipss2-managed-diagnostics-v2' ManagedSchema
    if($spec.pins -isnot [array] -or $spec.pins.Count -ne 4 -or $spec.changes -isnot [array] -or $spec.changes.Count -ne 3){Stop-S1 ManagedSchema}
    for($i=0;$i -lt 4;$i++){Assert-S1Value $spec.pins[$i] $origin.contract.checkouts[$i].revision ManagedPins}
    if(($spec.gitlinks|ConvertTo-Json -Depth 6 -Compress) -cne ($origin.contract.gitlinks|ConvertTo-Json -Depth 6 -Compress)){Stop-S1 ManagedGitlinks}
    $original=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach($row in $origin.rows){$original.Add($row.path,$row)}
    $changesByPath=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach($change in $spec.changes){
        Assert-S1Fields $change @('path','old','replacement','occurrences','originalSha256','derivedSha256')
        Assert-S1RelativePath $change.path
        if(!$original.ContainsKey($change.path)){Stop-S1 ManagedPath}
        $pre=if($changesByPath.ContainsKey($change.path)){$changesByPath[$change.path].derivedSha256}else{$original[$change.path].originalSha256}
        Assert-S1Value $change.originalSha256 $pre StageChain
        $changesByPath[$change.path]=$change
    }
    $bytes=[IO.File]::ReadAllBytes($manifestPath)
    $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
    $lines=$text -split '\r?\n' | Where-Object {$_ -cne ''}
    if($lines.Count -ne 1481){Stop-S1 ManagedRows}
    $rows=[Collections.Generic.List[object]]::new()
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($line in $lines){
        $parts=$line.Split([char]9)
        if($parts.Count -ne 5){Stop-S1 ManagedRows}
        $path=$parts[4];Assert-S1RelativePath $path
        if(!$seen.Add($path) -or !$original.ContainsKey($path)){Stop-S1 ManagedPath}
        $from=$original[$path]
        if($parts[0] -cne $from.originalSha256 -or $parts[2] -cne $from.mode -or $parts[3] -cne $from.gitBlob){Stop-S1 ManagedOrigin}
        $expected=if($changesByPath.ContainsKey($path)){$changesByPath[$path].derivedSha256}else{$from.originalSha256}
        if($parts[1] -cne $expected){Stop-S1 ManagedPostimage}
        $rows.Add([pscustomobject]@{originalSha256=$parts[0];derivedSha256=$parts[1];mode=$parts[2];gitBlob=$parts[3];path=$path})
    }
    return [pscustomobject]@{origin=$origin;spec=$spec;rows=$rows.ToArray();specSha256='4C83CDA89C029EFFF6EF88E3D33C2FEED57EB94B60882C4C1215CCD1BDA70E29';manifestSha256='3D372FE265E962D845B8E6D375707071A30662A112B0F531928A006EDDF7F7F2'}
}
function Assert-S1DirectorySet($Root,$Paths,$GitlinkPaths=@(),$Skip=@()) {
    $allowed=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($path in @($Paths)+@($GitlinkPaths)){
        $part=$path
        if($path -cnotin $GitlinkPaths){$slash=$path.LastIndexOf('/');$part=if($slash -ge 0){$path.Substring(0,$slash)}else{''}}
        while($part){[void]$allowed.Add($part);$slash=$part.LastIndexOf('/');$part=if($slash -ge 0){$part.Substring(0,$slash)}else{''}}
    }
    $queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue($Root)
    while($queue.Count){foreach($entry in [IO.DirectoryInfo]::new($queue.Dequeue()).EnumerateFileSystemInfos()){
        if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){Stop-S1 ReparsePoint}
        $relative=[IO.Path]::GetRelativePath($Root,$entry.FullName).Replace('\','/')
        if($relative -cin $Skip){continue}
        if(($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0){if(!$allowed.Contains($relative)){Stop-S1 DirectorySet};$queue.Enqueue($entry.FullName)}
    }}
}
function Test-S1ManagedOriginal($Root,$Original) {
    $managed=Read-S1Managed $Root
    $null=Test-S1OriginalSource $Root $Original
    Assert-S1DirectorySet $Original @($managed.rows.path) @($managed.origin.contract.gitlinks.path) @('.git','third_party/highway/.git','third_party/skcms/.git','third_party/libjpeg-turbo/.git')
    return $managed
}
function Test-S1ManagedDerivative($Root,$Derived) {
    $managed=Read-S1Managed $Root
    $files=Get-S1Files $Derived
    Assert-S1InputSet @($managed.rows.path) @($files.Keys)
    Assert-S1DirectorySet $Derived @($managed.rows.path)
    foreach($row in $managed.rows){if((Get-S1Hash ([IO.File]::ReadAllBytes($files[$row.path]))) -cne $row.derivedSha256){Stop-S1 DerivedPostimage}}
    return [pscustomobject]@{result='PASS';files=1481;modePolicy='Original Git tree modes bound to v2 manifest; no separate POSIX executable-bit assertion on Windows output';specSha256=$managed.specSha256;manifestSha256=$managed.manifestSha256}
}
function Get-S1ExpectedRoot($Root,$Relative,$Override) {
    $expected=Resolve-S1Path $Root $Relative
    if($Override -isnot [string]){Stop-S1 ScalarString}
    if($Override -cne ''){
        if(![IO.Path]::IsPathFullyQualified($Override)){Stop-S1 OutputRoot}
        Assert-S1NoReparse $Override
        if([IO.Path]::GetFullPath($Override).TrimEnd([char[]]@('/','\')) -cne $expected){Stop-S1 OutputRoot}
    }
    return $expected
}
