#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$R2EvidenceRoot,
    [Parameter(Mandatory)][string]$R3EvidenceRoot,
    [Parameter(Mandatory)][string]$R4BEvidenceRoot,
    [Parameter(Mandatory)][string]$R4DEvidenceRoot,
    [Parameter(Mandatory)][string]$V3EvidenceRoot,
    [Parameter(Mandatory)][string]$TestRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinarySdk2V4Package.psm1)
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$Root=[IO.Path]::GetFullPath($Root);$TestRoot=[IO.Path]::GetFullPath($TestRoot)
Assert-S1NoReparse $TestRoot
if(!$TestRoot.StartsWith((Join-Path $Root build/phase3d-sdk2-v4-tests-),[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $TestRoot)){throw 'New isolated test container required'}
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
function Plan {Read-S1Sdk4Plan $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot}
function Create($Path,$Auth){New-S1Sdk4Candidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $Path $Auth}
function Tree($Path){Test-S1Sdk4Tree $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $Path}
function Candidate($Path){Test-S1Sdk4Candidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $Path}
function Archive($Path){Test-S1Sdk4Zip $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $Path}
$profile=Read-S1StrictJson (Join-Path $Root packaging/public/profile-s1-binary-release-v4.json)
$authPath=Join-Path $Root packaging/public/authorization-s1-sdk2-v4-private-v1.json
$authText=[IO.File]::ReadAllText($authPath)
$protected=@($authPath,(Join-Path $Root packaging/public/profile-s1-binary-release-v4.json),(Join-Path $Root $profile.parentProfile.path),(Join-Path $Root $profile.decisionRecord.path))
$protected+=@($profile.files|ForEach-Object {Join-Path $Root $_.source})
$protected+=@($profile.r2Bindings|ForEach-Object {Join-Path $R2EvidenceRoot $_.path})
$protected+=@($profile.r3Bindings|ForEach-Object {Join-Path $R3EvidenceRoot $_.path})
$protected+=@($profile.r4bBindings|ForEach-Object {Join-Path $R4BEvidenceRoot $_.path})
$protected+=@($profile.r4dBindings|ForEach-Object {Join-Path $R4DEvidenceRoot $_.path})
$protected+=@($profile.v3Bindings|ForEach-Object {Join-Path $V3EvidenceRoot $_.path})
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
    if($r.files -ne 46 -or $r.PEMatchesR2 -ne 18 -or $r.unchangedFromV3 -ne 45 -or $r.legalApprovalGranted -or $r.publicDistributionAllowed -or $r.binaryReleaseApproved){throw 'Private acceptance'}
    $null=Candidate $good
}
Check 'ZIP, expansion, fixed documents, original licenses and marker match' {
    $null=Archive (Join-Path $good $profile.zipName);$null=Tree (Join-Path $good expanded)
    if([IO.File]::ReadAllText((Join-Path $good expanded/clipss2.portable)) -cne "clipss2-portable-v1`n"){throw 'Marker'}
}
Check 'Exactly 45 v3 files are inherited; only terms differ' {
    $sameCount=0
    foreach($file in $profile.files){
        $old=Join-Path $V3EvidenceRoot ('candidate/expanded/'+$file.path)
        $same=(Get-FileHash -LiteralPath $old).Hash -ceq $file.sha256
        if($same){$sameCount++}
        if($same -eq ($file.path -ceq 'MICROSOFT-COMPONENT-TERMS.txt')){throw 'Unexpected payload delta'}
    }
    if($sameCount -ne 45){throw 'Inheritance count'}
}
Check 'Seven sections; approved sections 4 and 6 match independent Owner-text hashes' {
    $new=[IO.File]::ReadAllText((Join-Path $good expanded/MICROSOFT-COMPONENT-TERMS.txt))
    $old=[IO.File]::ReadAllText((Join-Path $V3EvidenceRoot candidate/expanded/MICROSOFT-COMPONENT-TERMS.txt))
    if([regex]::Matches($new,'(?m)^[1-7]\. ').Count -ne 7){throw 'Section count'}
    # These expected values were measured from the Owner-supplied text, not the profile.
    $approved=@{4='B1E4CFE460C924676F102081C02CFBE8B84023FD4CE7738EACEE08592D6AF33B';6='C968D474579DC9775779406ACCE7F48D35139526DCED11EBF8CE5277621DF319'}
    foreach($n in @(4,6)){
        $section=[regex]::Match($new,('(?ms)^'+$n+'\. .*?(?=^\d\. |\z)')).Value.TrimEnd()+"`n"
        $hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes($section)))
        if($hash -cne $approved[$n]){throw 'Owner-approved text mismatch'}
    }
    foreach($n in @(1,2,3,5,7)){
        $pattern='(?ms)^'+$n+'\. .*?(?=^\d\. |\z)'
        if([regex]::Match($new,$pattern).Value -cne [regex]::Match($old,$pattern).Value){throw 'Retained section changed'}
    }
}
Check 'Scope, mandatory rights, supplier protection and absence of monetary cap' {
    $text=[IO.File]::ReadAllText((Join-Path $good expanded/MICROSOFT-COMPONENT-TERMS.txt))
    foreach($literal in @('applies only to the Covered SDK Code actually incorporated','does not apply to independently licensed clipss2-authored code','except to the extent that applicable law expressly permits','Microsoft and its suppliers are intended third-party beneficiaries','No monetary liability cap','or transfer the distributor''s SDK indemnification and defense obligations to end users')){if(!$text.Contains($literal)){throw 'Missing approved safeguard'}}
    if($text -match '\$5|five dollars|(?i)Microsoft approved template'){throw 'Unauthorized claim or cap'}
    if($profile.microsoftStatus.SDK2 -cne 'CLEAR IF' -or $profile.microsoftStatus.equivalentProtectionVerified -or $profile.microsoftStatus.recipientAgreementEstablished){throw 'Gate promotion'}
}
Check 'README and NOTICE are unchanged and reference the same packaged terms name' {
    foreach($name in @('README.md','THIRD-PARTY-NOTICES.md')){
        $text=[IO.File]::ReadAllText((Join-Path $good ('expanded/'+$name)))
        if(!$text.Contains('MICROSOFT-COMPONENT-TERMS.txt')){throw 'Missing terms reference'}
        if((Get-FileHash -LiteralPath (Join-Path $good ('expanded/'+$name))).Hash -cne (Get-FileHash -LiteralPath (Join-Path $V3EvidenceRoot ('candidate/expanded/'+$name))).Hash){throw 'Unnecessary document change'}
    }
}
Check 'New terms validator accepts the fixed approved text and rejects unapproved section edits' {
    $module=Get-Module S1BinarySdk2V4Package
    $old=[IO.File]::ReadAllText((Join-Path $V3EvidenceRoot candidate/expanded/MICROSOFT-COMPONENT-TERMS.txt))
    $new=[IO.File]::ReadAllText((Join-Path $good expanded/MICROSOFT-COMPONENT-TERMS.txt))
    & $module {param($N,$O,$V) Assert-Sdk4Terms $N $O $V} $new $old $profile.termsRevision
    Refuse 'Unapproved section 1 edit refused' {& $module {param($N,$O,$V) Assert-Sdk4Terms $N $O $V} ($new.Replace('existing SDK-1 review','replaced SDK-1 review')) $old $profile.termsRevision} 'S1:Sdk4TermsInheritance'
    Refuse 'Approved section 4 mutation refused' {& $module {param($N,$O,$V) Assert-Sdk4Terms $N $O $V} ($new.Replace('do not circumvent','may circumvent')) $old $profile.termsRevision} 'S1:Sdk4ApprovedSection'
    Refuse 'Approved section 6 mutation refused' {& $module {param($N,$O,$V) Assert-Sdk4Terms $N $O $V} ($new.Replace('direct, indirect','indirect')) $old $profile.termsRevision} 'S1:Sdk4ApprovedSection'
}
foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','githubReleaseAllowed','signingAllowed','legalApprovalGranted','reuseAllowed','legacyFallbackAllowed','candidateGenerationAuthorized','ownerTermsTextApproved')){
    $text=if($key -in @('candidateGenerationAuthorized','ownerTermsTextApproved')){$authText.Replace(('"'+$key+'": true'),('"'+$key+'": false'))}else{$authText.Replace(('"'+$key+'": false'),('"'+$key+'": true'))}
    if($text -ceq $authText){throw 'Fixture did not mutate'}
    $path=Join-Path $TestRoot ($key+'.json');[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    $output=Join-Path $TestRoot ('refused-'+$key)
    Refuse "Authorization rejects $key mutation" {Create $output $path} 'S1:RawIdentity'
    if(Test-Path -LiteralPath $output){throw 'Rejected authority wrote output'}
}
foreach($path in @('packaging/public/profile-s1-binary-private-v1.json','packaging/public/profile-s1-binary-release-v1.json','packaging/public/authorization-s1-r4b-private-v1.json','packaging/public/authorization-s1-r4d-final-private-v1.json','packaging/public/authorization-s1-sdk2-private-v1.json','packaging/public/profile-s1-binary-release-v3.json')){Refuse "No legacy authorization fallback: $path" {Read-S1Sdk4Authorization $Root $path} 'S1:RawIdentity'}
Refuse 'Missing authorization fails closed' {Create (Join-Path $TestRoot no-auth) (Join-Path $TestRoot absent.json)} 'S1:FileMissing'
$missing=Join-Path $TestRoot missing-profile
[void][IO.Directory]::CreateDirectory((Join-Path $missing packaging/public))
[IO.File]::Copy($authPath,(Join-Path $missing packaging/public/authorization-s1-sdk2-v4-private-v1.json),$false)
Refuse 'Missing new profile fails closed without old profile fallback' {Read-S1Sdk4Authorization $missing} 'S1:FileMissing'
foreach($level in @('r2','r3','r4b','r4d','v3')){
    $fake=Join-Path $TestRoot ('fake-'+$level);[void][IO.Directory]::CreateDirectory($fake)
    [IO.File]::WriteAllText((Join-Path $fake result.json),'{}')
    $e2=$R2EvidenceRoot;$e3=$R3EvidenceRoot;$eb=$R4BEvidenceRoot;$ed=$R4DEvidenceRoot;$ev=$V3EvidenceRoot
    switch($level){r2{$e2=$fake};r3{$e3=$fake};r4b{$eb=$fake};r4d{$ed=$fake};v3{$ev=$fake}}
    Refuse "$level evidence substitution refused" {Read-S1Sdk4Plan $Root $e2 $e3 $eb $ed $ev} 'S1:(RawIdentity|FileMissing)'
}
$existing=Join-Path $TestRoot existing;[void][IO.Directory]::CreateDirectory($existing)
Refuse 'Existing empty output refused' {Create $existing} 'S1:Sdk4:ExistingOutput'
[IO.File]::WriteAllText((Join-Path $existing sentinel),'preserve')
Refuse 'Existing unknown output refused' {Create $existing} 'S1:Sdk4:ExistingOutput'
Check 'Existing contents unchanged' {if([IO.File]::ReadAllText((Join-Path $existing sentinel)) -cne 'preserve'){throw 'Overwrite'}}
Refuse 'Unauthorized output scope refused' {Create (Join-Path $Root build/forbidden-r4d)} 'S1:Sdk4:OutputScope'
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
Refuse 'Old v3 ZIP cannot substitute for v4 ZIP' {Archive (Join-Path $V3EvidenceRoot ('candidate/'+$profile.zipName))} 'S1:Sdk2:Zip'
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
    if($change -eq 'receipt-revision'){$text=$text.Replace('ffac56bd1713d5afe659cb16dc9fe2e34e3a6649','93545d458da33cef397fb77458336ea06339dcb6')}
    elseif($change -eq 'receipt-legal'){$text=$text.Replace('"legalApprovalGranted": false','"legalApprovalGranted": true')}
    else{$text=$text.Replace('"publicDistributionAllowed": false','"publicDistributionAllowed": true')}
    [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    Refuse "Candidate rejects $change" {Candidate $fixture} 'S1:Sdk4(Owner|Receipt)'
}
Check 'Post-ZIP failure preserves partial output and never issues acceptance' {
    $module=Get-Module S1BinarySdk2V4Package;$original=& $module {(Get-Command Read-Sdk4Archive).ScriptBlock};$partial=Join-Path $TestRoot partial
    try{
        & $module {function script:Read-Sdk4Archive {throw 'S1:Sdk4:InjectedAdmissionFailure'}}
        Refuse 'Archive admission failure stops generation' {Create $partial} 'S1:Sdk4:InjectedAdmissionFailure'
    }finally{& $module {param($Body) Set-Item Function:script:Read-Sdk4Archive $Body} $original}
    if(!(Test-Path -LiteralPath (Join-Path $partial $profile.zipName)) -or (Test-Path -LiteralPath (Join-Path $partial acceptance.json)) -or (Test-Path -LiteralPath (Join-Path $partial expanded))){throw 'Partial output admission'}
    Refuse 'Partial output cannot be accepted' {Candidate $partial} 'S1:ScopeFiles'
}
Check 'Protected inputs, 18 PE and previous evidence retain bytes and write times' {
    foreach($path in $before.Keys){if((Get-FileHash -LiteralPath $path).Hash -cne $before[$path].sha -or (Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks -ne $before[$path].write){throw 'Protected input changed'}}
}
$report=[ordered]@{passed=$results.Count;failed=0;scope='Phase 3-D SDK-2 v4 private Packaging only';tests=@($results.ToArray());binaryReleaseApproved=$false;publicDistributionAllowed=$false}
[IO.File]::WriteAllText((Join-Path $TestRoot results.json),($report|ConvertTo-Json -Depth 16).Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]$report|Select-Object passed,failed,scope,binaryReleaseApproved,publicDistributionAllowed|ConvertTo-Json
