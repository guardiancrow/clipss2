#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$R2EvidenceRoot,
    [Parameter(Mandatory)][string]$R3EvidenceRoot,
    [Parameter(Mandatory)][string]$R4BEvidenceRoot,
    [Parameter(Mandatory)][string]$R4DEvidenceRoot,
    [Parameter(Mandatory)][string]$TestRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinarySdk2Package.psm1)
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$Root=[IO.Path]::GetFullPath($Root);$TestRoot=[IO.Path]::GetFullPath($TestRoot)
Assert-S1NoReparse $TestRoot
if(!$TestRoot.StartsWith((Join-Path $Root build/phase3c-sdk2-tests-),[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $TestRoot)){throw 'New isolated test container required'}
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
function Plan {Read-S1Sdk2Plan $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot}
function Create($Path,$Auth){New-S1Sdk2Candidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $Path $Auth}
function Tree($Path){Test-S1Sdk2Tree $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $Path}
function Candidate($Path){Test-S1Sdk2Candidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $Path}
function Archive($Path){Test-S1Sdk2Zip $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $Path}
$profile=Read-S1StrictJson (Join-Path $Root packaging/public/profile-s1-binary-release-v3.json)
$authPath=Join-Path $Root packaging/public/authorization-s1-sdk2-private-v1.json
$authText=[IO.File]::ReadAllText($authPath)
$protected=@($authPath,(Join-Path $Root packaging/public/profile-s1-binary-release-v3.json),(Join-Path $Root $profile.parentProfile.path),(Join-Path $Root $profile.decisionRecord.path))
$protected+=@($profile.files|ForEach-Object {Join-Path $Root $_.source})
$protected+=@($profile.r2Bindings|ForEach-Object {Join-Path $R2EvidenceRoot $_.path})
$protected+=@($profile.r3Bindings|ForEach-Object {Join-Path $R3EvidenceRoot $_.path})
$protected+=@($profile.r4bBindings|ForEach-Object {Join-Path $R4BEvidenceRoot $_.path})
$protected+=@($profile.r4dBindings|ForEach-Object {Join-Path $R4DEvidenceRoot $_.path})
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
    if($r.files -ne 46 -or $r.PEMatchesR2 -ne 18 -or $r.unchangedFromR4D -ne 43 -or $r.legalApprovalGranted -or $r.publicDistributionAllowed -or $r.binaryReleaseApproved){throw 'Private acceptance'}
    $null=Candidate $good
}
Check 'ZIP, expansion, fixed documents, original licenses and marker match' {
    $null=Archive (Join-Path $good $profile.zipName);$null=Tree (Join-Path $good expanded)
    if([IO.File]::ReadAllText((Join-Path $good expanded/clipss2.portable)) -cne "clipss2-portable-v1`n"){throw 'Marker'}
}
Check 'Exactly 43 unchanged R4-D files and 18 R2 PE are inherited' {
    $sameCount=0
    foreach($file in $profile.files){
        if($file.path -ceq 'MICROSOFT-COMPONENT-TERMS.txt'){continue}
        $old=Join-Path $R4DEvidenceRoot ('candidate/expanded/'+$file.path)
        $same=(Get-FileHash -LiteralPath $old).Hash -ceq $file.sha256
        if($same){$sameCount++}
        if($same -eq ($file.path -cin @('README.md','THIRD-PARTY-NOTICES.md'))){throw 'Unexpected payload delta'}
    }
    if($sameCount -ne 43){throw 'Inheritance count'}
}
Check 'Public documents retain full attribution, unsigned and limited evidence distinctions' {
    $old=[IO.File]::ReadAllText((Join-Path $Root packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v2.md))
    $new=[IO.File]::ReadAllText((Join-Path $Root packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v3.md))
    if(!$new.StartsWith($old,[StringComparison]::Ordinal)){throw 'Attribution changed'}
    $readme=[IO.File]::ReadAllText((Join-Path $Root packaging/binary/README-portable-s1-release-v3.md))
    foreach($literal in @('コード署名を付していません','最低必要versionとは断定せず','ユーザーから報告','本ZIP全体をその別PCで検証したという主張ではありません','MICROSOFT-COMPONENT-TERMS.txt')){if(!$readme.Contains($literal)){throw 'Missing public distinction'}}
}
Check 'Terms implement seven sections, limited SDK scope and selected option A' {
    $text=[IO.File]::ReadAllText((Join-Path $good expanded/MICROSOFT-COMPONENT-TERMS.txt))
    $headings=[regex]::Matches($text,'(?m)^[1-7]\. ')
    if($headings.Count -ne 7){throw 'Terms structure'}
    foreach($literal in @('WIN10SDK.RTM.AUG_2018_en-US','Windows SDK 10.0.26100.0','No separate contractual exclusion of damages or monetary','Microsoft Visual C++ startup code','not a Microsoft','legally effective agreement are distinct matters')){if(!$text.Contains($literal)){throw 'Terms scope or option A'}}
    if($text -match '(?i)must not reverse|shall not reverse|five dollars|\$5|review draft|pending final choice'){throw 'Unselected restriction'}
    if($profile.microsoftStatus.SDK2 -cne 'CLEAR IF' -or $profile.microsoftStatus.equivalentProtectionVerified -or $profile.microsoftStatus.recipientAgreementEstablished){throw 'Legal gate promotion'}
}
foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','githubReleaseAllowed','signingAllowed','legalApprovalGranted','reuseAllowed','legacyFallbackAllowed','candidateGenerationAuthorized')){
    $text=if($key -eq 'candidateGenerationAuthorized'){$authText.Replace(('"'+$key+'": true'),('"'+$key+'": false'))}else{$authText.Replace(('"'+$key+'": false'),('"'+$key+'": true'))}
    if($text -ceq $authText){throw 'Fixture did not mutate'}
    $path=Join-Path $TestRoot ($key+'.json');[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    $output=Join-Path $TestRoot ('refused-'+$key)
    Refuse "Authorization rejects $key mutation" {Create $output $path} 'S1:RawIdentity'
    if(Test-Path -LiteralPath $output){throw 'Rejected authority wrote output'}
}
foreach($path in @('packaging/public/profile-s1-binary-private-v1.json','packaging/public/profile-s1-binary-release-v1.json','packaging/public/authorization-s1-r4b-private-v1.json','packaging/public/authorization-s1-r4d-final-private-v1.json')){Refuse "No legacy authorization fallback: $path" {Read-S1Sdk2Authorization $Root $path} 'S1:RawIdentity'}
Refuse 'Missing authorization fails closed' {Create (Join-Path $TestRoot no-auth) (Join-Path $TestRoot absent.json)} 'S1:FileMissing'
$missing=Join-Path $TestRoot missing-profile
[void][IO.Directory]::CreateDirectory((Join-Path $missing packaging/public))
[IO.File]::Copy($authPath,(Join-Path $missing packaging/public/authorization-s1-sdk2-private-v1.json),$false)
Refuse 'Missing new profile fails closed without old profile fallback' {Read-S1Sdk2Authorization $missing} 'S1:FileMissing'
foreach($level in @('r2','r3','r4b','r4d')){
    $fake=Join-Path $TestRoot ('fake-'+$level);[void][IO.Directory]::CreateDirectory($fake)
    [IO.File]::WriteAllText((Join-Path $fake result.json),'{}')
    $e2=$R2EvidenceRoot;$e3=$R3EvidenceRoot;$eb=$R4BEvidenceRoot;$ed=$R4DEvidenceRoot
    switch($level){r2{$e2=$fake};r3{$e3=$fake};r4b{$eb=$fake};r4d{$ed=$fake}}
    Refuse "$level evidence substitution refused" {Read-S1Sdk2Plan $Root $e2 $e3 $eb $ed} 'S1:(RawIdentity|FileMissing)'
}
$existing=Join-Path $TestRoot existing;[void][IO.Directory]::CreateDirectory($existing)
Refuse 'Existing empty output refused' {Create $existing} 'S1:Sdk2:ExistingOutput'
[IO.File]::WriteAllText((Join-Path $existing sentinel),'preserve')
Refuse 'Existing unknown output refused' {Create $existing} 'S1:Sdk2:ExistingOutput'
Check 'Existing contents unchanged' {if([IO.File]::ReadAllText((Join-Path $existing sentinel)) -cne 'preserve'){throw 'Overwrite'}}
Refuse 'Unauthorized output scope refused' {Create (Join-Path $Root build/forbidden-r4d)} 'S1:Sdk2:OutputScope'
foreach($extra in @('settings.conf','product.pdb','library.lib','sample.png','audit.log','PRIVATE/notes.md','authorization.json','msvcp140.dll','vcruntime140.dll','kernel32.dll','vc_redist.x64.exe')){
    $fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot ('extra-'+$extra.Replace('/','-')))
    $path=Join-Path $fixture $extra;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path));[IO.File]::WriteAllText($path,'synthetic')
    Refuse "Payload rejects extra $extra" {Tree $fixture} 'S1:ScopeFiles'
}
foreach($name in @('clipss2.exe','licenses/jpegli/PATENTS','README.md','THIRD-PARTY-NOTICES.md','MICROSOFT-COMPONENT-TERMS.txt','clipss2.portable')){
    $fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot ('modified-'+$name.Replace('/','-')))
    Mutate-Byte (Join-Path $fixture $name)
    Refuse "Payload rejects modified $name" {Tree $fixture} 'S1:RawIdentity'
}
$fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot reparse)
$target=Join-Path $TestRoot owned-junction-target;[void][IO.Directory]::CreateDirectory($target)
$null=New-Item -ItemType Junction -Path (Join-Path $fixture link) -Target $target
Refuse 'Reparse payload refused' {Tree $fixture} 'S1:ReparsePoint'
Refuse 'Reparse output refused' {Create (Join-Path $fixture link/candidate)} 'S1:ReparsePoint'
Refuse 'R4-B ZIP cannot substitute for final ZIP' {Archive (Join-Path $R4BEvidenceRoot candidate/clipss2-s1-v2-93545d4-release-candidate.zip)} 'S1:Sdk2:Zip'
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
    Refuse "ZIP rejects $change" {Archive $path} 'S1:(Sdk2:Zip|RelativePath)'
}
foreach($change in @('receipt-public','owner-public','receipt-revision','receipt-legal')){
    $fixture=Copy-Tree $good (Join-Path $TestRoot $change)
    $name=if($change -eq 'owner-public'){'owner.json'}else{'acceptance.json'};$path=Join-Path $fixture $name;$text=[IO.File]::ReadAllText($path)
    if($change -eq 'receipt-revision'){$text=$text.Replace('f8cf793b6dfbda951e051b90a2147d5c3a3103e3','93545d458da33cef397fb77458336ea06339dcb6')}
    elseif($change -eq 'receipt-legal'){$text=$text.Replace('"legalApprovalGranted": false','"legalApprovalGranted": true')}
    else{$text=$text.Replace('"publicDistributionAllowed": false','"publicDistributionAllowed": true')}
    [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    Refuse "Candidate rejects $change" {Candidate $fixture} 'S1:Sdk2(Owner|Receipt)'
}
Check 'Post-ZIP failure preserves partial output and never issues acceptance' {
    $module=Get-Module S1BinarySdk2Package;$original=& $module {(Get-Command Read-Sdk2Archive).ScriptBlock};$partial=Join-Path $TestRoot partial
    try{
        & $module {function script:Read-Sdk2Archive {throw 'S1:Sdk2:InjectedAdmissionFailure'}}
        Refuse 'Archive admission failure stops generation' {Create $partial} 'S1:Sdk2:InjectedAdmissionFailure'
    }finally{& $module {param($Body) Set-Item Function:script:Read-Sdk2Archive $Body} $original}
    if(!(Test-Path -LiteralPath (Join-Path $partial $profile.zipName)) -or (Test-Path -LiteralPath (Join-Path $partial acceptance.json)) -or (Test-Path -LiteralPath (Join-Path $partial expanded))){throw 'Partial output admission'}
    Refuse 'Partial output cannot be accepted' {Candidate $partial} 'S1:ScopeFiles'
}
Check 'Protected inputs, 18 PE and previous evidence retain bytes and write times' {
    foreach($path in $before.Keys){if((Get-FileHash -LiteralPath $path).Hash -cne $before[$path].sha -or (Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks -ne $before[$path].write){throw 'Protected input changed'}}
}
$report=[ordered]@{passed=$results.Count;failed=0;scope='Phase 3-C SDK-2 private Packaging only';tests=@($results.ToArray());binaryReleaseApproved=$false;publicDistributionAllowed=$false}
[IO.File]::WriteAllText((Join-Path $TestRoot results.json),($report|ConvertTo-Json -Depth 16).Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]$report|Select-Object passed,failed,scope,binaryReleaseApproved,publicDistributionAllowed|ConvertTo-Json
