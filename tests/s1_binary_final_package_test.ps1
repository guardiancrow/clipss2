#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$R2EvidenceRoot,
    [Parameter(Mandatory)][string]$R3EvidenceRoot,
    [Parameter(Mandatory)][string]$R4BEvidenceRoot,
    [Parameter(Mandatory)][string]$TestRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinaryFinalPackage.psm1)
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$Root=[IO.Path]::GetFullPath($Root);$TestRoot=[IO.Path]::GetFullPath($TestRoot)
Assert-S1NoReparse $TestRoot
if(!$TestRoot.StartsWith((Join-Path $Root build/r4-d-final-tests-),[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $TestRoot)){throw 'New isolated test container required'}
[void][IO.Directory]::CreateDirectory($TestRoot)
$results=[Collections.Generic.List[object]]::new()
function Check($Name,[scriptblock]$Action){& $Action;$results.Add([ordered]@{name=$Name;result='PASS'});Write-Host "PASS $Name"}
function Refuse($Name,[scriptblock]$Action,$Pattern){
    $message=$null;try{$null=& $Action}catch{$message=$_.Exception.Message}
    if(!$message -or $message -notmatch $Pattern){throw "Expected refusal: $Name; actual=$message"}
    $results.Add([ordered]@{name=$Name;result='PASS';refusal=$message});Write-Host "PASS $Name"
}
function Copy-Tree($Source,$Destination){
    [void][IO.Directory]::CreateDirectory($Destination)
    foreach($file in (Get-S1Files $Source).GetEnumerator()){
        $path=Join-Path $Destination $file.Key
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::Copy($file.Value,$path,$false)
    }
    return $Destination
}
function Mutate-Byte($Path){
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite)
    try{$value=$stream.ReadByte();$stream.Position=0;$stream.WriteByte($value -bxor 1)}finally{$stream.Dispose()}
}
function Plan {Read-S1FinalPlan $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot}
function Create($Path,$Auth){New-S1FinalCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $Path $Auth}
function Tree($Path){Test-S1FinalTree $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $Path}
function Candidate($Path){Test-S1FinalCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $Path}
function Archive($Path){Test-S1FinalZip $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $Path}
$profile=Read-S1StrictJson (Join-Path $Root packaging/public/profile-s1-binary-release-v2.json)
$authPath=Join-Path $Root packaging/public/authorization-s1-r4d-final-private-v1.json
$authText=[IO.File]::ReadAllText($authPath)
$protected=@($authPath,(Join-Path $Root packaging/public/profile-s1-binary-release-v2.json),(Join-Path $Root $profile.parentProfile.path),(Join-Path $Root $profile.decisionRecord.path))
$protected+=@($profile.files|ForEach-Object {Join-Path $Root $_.source})
$protected+=@($profile.r2Bindings|ForEach-Object {Join-Path $R2EvidenceRoot $_.path})
$protected+=@($profile.r3Bindings|ForEach-Object {Join-Path $R3EvidenceRoot $_.path})
$protected+=@($profile.r4bBindings|ForEach-Object {Join-Path $R4BEvidenceRoot $_.path})
$protected+=@($profile.sourceNoticeBindings|ForEach-Object {Join-Path $Root $_.path})
$protected+=@(Get-ChildItem -LiteralPath (Join-Path $Root packaging/public) -File|ForEach-Object FullName)
$protected+=@('packaging/S1BinaryPackage.psm1','packaging/S1BinaryReleasePlan.psm1','packaging/S1BinaryReleasePackage.psm1','tools/package_s1_binary_release.ps1','packaging/binary/README-portable-s1-release-v1.md','packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v1.md'|ForEach-Object {Join-Path $Root $_})
$before=@{}
foreach($path in $protected){$before[$path]=[ordered]@{sha=(Get-FileHash -LiteralPath $path).Hash;write=(Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks}}
Check 'PlanOnly makes no output and separates private generation, publication and revisions' {
    $entries=@([IO.Directory]::EnumerateFileSystemEntries($TestRoot));$plan=Plan
    if(!$plan.candidateGenerationAuthorized -or $plan.binaryReleaseApproved -or $plan.publicDistributionAllowed -or $plan.packagingRevision -ceq $plan.productSourceRevision){throw 'Approval or revision confusion'}
    Assert-S1InputSet $entries @([IO.Directory]::EnumerateFileSystemEntries($TestRoot))
}
$good=Join-Path $TestRoot good
Check 'Private final candidate and independent full receipt acceptance' {
    $r=Create $good
    if($r.files -ne 45 -or $r.PEMatchesR2 -ne 18 -or $r.unchangedFromR4B -ne 43 -or $r.legalApprovalGranted -or $r.publicDistributionAllowed -or $r.binaryReleaseApproved){throw 'Private acceptance'}
    $null=Candidate $good
}
Check 'ZIP, expansion, fixed documents, original licenses and marker match' {
    $null=Archive (Join-Path $good $profile.zipName);$null=Tree (Join-Path $good expanded)
    if([IO.File]::ReadAllText((Join-Path $good expanded/clipss2.portable)) -cne "clipss2-portable-v1`n"){throw 'Marker'}
}
Check 'Only README and NOTICE differ from accepted R4-B payload' {
    foreach($file in $profile.files){
        $old=Join-Path $R4BEvidenceRoot ('candidate/expanded/'+$file.path)
        $same=(Get-FileHash -LiteralPath $old).Hash -ceq $file.sha256
        if($same -eq ($file.path -cin @('README.md','THIRD-PARTY-NOTICES.md'))){throw 'Unexpected payload delta'}
    }
}
Check 'Public documents retain attribution, unsigned and limited evidence distinctions' {
    $old=[IO.File]::ReadAllText((Join-Path $Root packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v1.md))
    $new=[IO.File]::ReadAllText((Join-Path $Root packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v2.md))
    $body=$old.Substring($old.IndexOf('clipss2自作部分:'),$old.IndexOf('## Microsoftコードの区分と残件')-$old.IndexOf('clipss2自作部分:'))
    if(!$new.Contains($body)){throw 'Attribution changed'}
    $readme=[IO.File]::ReadAllText((Join-Path $Root packaging/binary/README-portable-s1-release-v2.md))
    if(($new+$readme) -match '草案|レビュー用|MS-002|LEGAL PASS|Microsoft承認済み'){throw 'Unwanted public assertion'}
    foreach($literal in @('コード署名を付していません','最低必要versionとは断定せず','ユーザーから報告','本ZIP全体をその別PCで検証したという主張ではありません')){if(!$readme.Contains($literal)){throw 'Missing public distinction'}}
}
foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','githubReleaseAllowed','signingAllowed','legalApprovalGranted','reuseAllowed','legacyFallbackAllowed','candidateGenerationAuthorized')){
    $text=if($key -eq 'candidateGenerationAuthorized'){$authText.Replace(('"'+$key+'": true'),('"'+$key+'": false'))}else{$authText.Replace(('"'+$key+'": false'),('"'+$key+'": true'))}
    if($text -ceq $authText){throw 'Fixture did not mutate'}
    $path=Join-Path $TestRoot ($key+'.json');[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    $output=Join-Path $TestRoot ('refused-'+$key)
    Refuse "Authorization rejects $key mutation" {Create $output $path} 'S1:RawIdentity'
    if(Test-Path -LiteralPath $output){throw 'Rejected authority wrote output'}
}
foreach($path in @('packaging/public/profile-s1-binary-private-v1.json','packaging/public/profile-s1-binary-release-v1.json','packaging/public/authorization-s1-r4b-private-v1.json')){Refuse "No legacy authorization fallback: $path" {Read-S1FinalAuthorization $Root $path} 'S1:RawIdentity'}
Refuse 'Missing authorization fails closed' {Create (Join-Path $TestRoot no-auth) (Join-Path $TestRoot absent.json)} 'S1:FileMissing'
$missing=Join-Path $TestRoot missing-profile
[void][IO.Directory]::CreateDirectory((Join-Path $missing packaging/public))
[IO.File]::Copy($authPath,(Join-Path $missing packaging/public/authorization-s1-r4d-final-private-v1.json),$false)
Refuse 'Missing new profile fails closed without old profile fallback' {Read-S1FinalAuthorization $missing} 'S1:FileMissing'
foreach($level in @('r2','r3','r4b')){
    $fake=Join-Path $TestRoot ('fake-'+$level);[void][IO.Directory]::CreateDirectory($fake)
    if($level -eq 'r2'){[IO.File]::Copy((Join-Path $R2EvidenceRoot identity.json),(Join-Path $fake identity.json),$false)}
    [IO.File]::WriteAllText((Join-Path $fake result.json),'{}')
    Refuse "$level evidence substitution refused" {
        switch($level){
            r2 {Read-S1FinalPlan $Root $fake $R3EvidenceRoot $R4BEvidenceRoot}
            r3 {Read-S1FinalPlan $Root $R2EvidenceRoot $fake $R4BEvidenceRoot}
            r4b {Read-S1FinalPlan $Root $R2EvidenceRoot $R3EvidenceRoot $fake}
        }
    } 'S1:RawIdentity'
}
$existing=Join-Path $TestRoot existing;[void][IO.Directory]::CreateDirectory($existing)
Refuse 'Existing empty output refused' {Create $existing} 'S1:Final:ExistingOutput'
[IO.File]::WriteAllText((Join-Path $existing sentinel),'preserve')
Refuse 'Existing unknown output refused' {Create $existing} 'S1:Final:ExistingOutput'
Check 'Existing contents unchanged' {if([IO.File]::ReadAllText((Join-Path $existing sentinel)) -cne 'preserve'){throw 'Overwrite'}}
Refuse 'Unauthorized output scope refused' {Create (Join-Path $Root build/forbidden-r4d)} 'S1:Final:OutputScope'
foreach($extra in @('settings.conf','product.pdb','library.lib','sample.png','audit.log','PRIVATE/notes.md','authorization.json')){
    $fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot ('extra-'+$extra.Replace('/','-')))
    $path=Join-Path $fixture $extra;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path));[IO.File]::WriteAllText($path,'synthetic')
    Refuse "Payload rejects extra $extra" {Tree $fixture} 'S1:ScopeFiles'
}
foreach($name in @('clipss2.exe','licenses/jpegli/PATENTS','README.md','THIRD-PARTY-NOTICES.md')){
    $fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot ('modified-'+$name.Replace('/','-')))
    Mutate-Byte (Join-Path $fixture $name)
    Refuse "Payload rejects modified $name" {Tree $fixture} 'S1:RawIdentity'
}
$fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot reparse)
$target=Join-Path $TestRoot owned-junction-target;[void][IO.Directory]::CreateDirectory($target)
$null=New-Item -ItemType Junction -Path (Join-Path $fixture link) -Target $target
Refuse 'Reparse payload refused' {Tree $fixture} 'S1:ReparsePoint'
Refuse 'Reparse output refused' {Create (Join-Path $fixture link/candidate)} 'S1:ReparsePoint'
Refuse 'R4-B ZIP cannot substitute for final ZIP' {Archive (Join-Path $R4BEvidenceRoot candidate/clipss2-s1-v2-93545d4-release-candidate.zip)} 'S1:Binary:Zip'
foreach($change in @('extra','traversal','duplicate','content')){
    $path=Join-Path $TestRoot ('invalid-'+$change+'.zip');$stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew)
    $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try{
        for($i=0;$i -lt $profile.files.Count;$i++){
            $file=$profile.files[$i];$name=$file.path
            if($i -eq 0 -and $change -eq 'traversal'){$name='../escape'}
            if($i -eq 0 -and $change -eq 'duplicate'){$name=$profile.files[1].path.ToUpperInvariant()}
            $entry=$archive.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal);$entry.LastWriteTime=[DateTimeOffset]'2026-10-23T09:00:00+09:00';$entry.ExternalAttributes=32
            $bytes=[IO.File]::ReadAllBytes((Join-Path $good ('payload/'+$file.path)))
            if($change -eq 'content' -and $file.path -ceq 'README.md'){$bytes[0]=$bytes[0] -bxor 1}
            $out=$entry.Open();try{$out.Write($bytes,0,$bytes.Length)}finally{$out.Dispose()}
        }
        if($change -eq 'extra'){$null=$archive.CreateEntry('settings.conf')}
    }finally{$archive.Dispose();$stream.Dispose()}
    Refuse "ZIP rejects $change" {Archive $path} 'S1:(Binary:Zip|RelativePath)'
}
foreach($change in @('receipt-public','owner-public','receipt-revision','receipt-legal')){
    $fixture=Copy-Tree $good (Join-Path $TestRoot $change)
    $name=if($change -eq 'owner-public'){'owner.json'}else{'acceptance.json'};$path=Join-Path $fixture $name;$text=[IO.File]::ReadAllText($path)
    if($change -eq 'receipt-revision'){$text=$text.Replace('2009cfec9ae43c5af79579536301de1af7650030','93545d458da33cef397fb77458336ea06339dcb6')}
    elseif($change -eq 'receipt-legal'){$text=$text.Replace('"legalApprovalGranted": false','"legalApprovalGranted": true')}
    else{$text=$text.Replace('"publicDistributionAllowed": false','"publicDistributionAllowed": true')}
    [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    Refuse "Candidate rejects $change" {Candidate $fixture} 'S1:Final(Owner|Receipt)'
}
Check 'Post-ZIP failure preserves partial output and never issues acceptance' {
    $module=Get-Module S1BinaryFinalPackage;$original=& $module {(Get-Command Read-FinalArchive).ScriptBlock};$partial=Join-Path $TestRoot partial
    try{
        & $module {function script:Read-FinalArchive {throw 'S1:Final:InjectedAdmissionFailure'}}
        Refuse 'Archive admission failure stops generation' {Create $partial} 'S1:Final:InjectedAdmissionFailure'
    }finally{& $module {param($Body) Set-Item Function:script:Read-FinalArchive $Body} $original}
    if(!(Test-Path -LiteralPath (Join-Path $partial $profile.zipName)) -or (Test-Path -LiteralPath (Join-Path $partial acceptance.json)) -or (Test-Path -LiteralPath (Join-Path $partial expanded))){throw 'Partial output admission'}
    Refuse 'Partial output cannot be accepted' {Candidate $partial} 'S1:ScopeFiles'
}
Check 'Protected inputs, 18 PE and previous evidence retain bytes and write times' {
    foreach($path in $before.Keys){if((Get-FileHash -LiteralPath $path).Hash -cne $before[$path].sha -or (Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks -ne $before[$path].write){throw 'Protected input changed'}}
}
$report=[ordered]@{passed=$results.Count;failed=0;scope='R4-D private final Packaging only';tests=@($results.ToArray());binaryReleaseApproved=$false;publicDistributionAllowed=$false}
[IO.File]::WriteAllText((Join-Path $TestRoot results.json),($report|ConvertTo-Json -Depth 16).Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]$report|Select-Object passed,failed,scope,binaryReleaseApproved,publicDistributionAllowed|ConvertTo-Json
