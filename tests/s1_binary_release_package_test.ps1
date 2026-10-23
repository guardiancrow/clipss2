#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$R2EvidenceRoot,
    [Parameter(Mandatory)][string]$R3EvidenceRoot,
    [Parameter(Mandatory)][string]$TestRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinaryReleasePackage.psm1)
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinaryReleasePlan.psm1)
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$Root=[IO.Path]::GetFullPath($Root);$TestRoot=[IO.Path]::GetFullPath($TestRoot)
Assert-S1NoReparse $TestRoot
if(!$TestRoot.StartsWith((Join-Path $Root build/r4-b-package-tests-),[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $TestRoot)){throw 'New isolated test container required'}
[void][IO.Directory]::CreateDirectory($TestRoot)
$results=[Collections.Generic.List[object]]::new()
function Check($Name,[scriptblock]$Action){
    & $Action;$results.Add([ordered]@{name=$Name;result='PASS'});Write-Host "PASS $Name"
}
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
$profile=Read-S1BinaryReleaseProfile $Root
$authPath=Join-Path $Root packaging/public/authorization-s1-r4b-private-v1.json
$authText=[IO.File]::ReadAllText($authPath)
$protected=@((Join-Path $Root $profile.parentProfile.path),(Join-Path $Root packaging/S1BinaryPackage.psm1),
    (Join-Path $Root packaging/S1BinaryReleasePlan.psm1),(Join-Path $Root packaging/public/profile-s1-binary-release-v1.json),
    (Join-Path $Root docs/R4-A-BINARY-RELEASE-POLICY.md),$authPath)
$protected+=@($profile.files|ForEach-Object {Join-Path $Root $_.source})
$protected+=@($profile.r2Bindings|ForEach-Object {Join-Path $R2EvidenceRoot $_.path})
$protected+=@($profile.r3Bindings|ForEach-Object {Join-Path $R3EvidenceRoot $_.path})
$protected+=@($profile.sourceNoticeBindings|ForEach-Object {Join-Path $Root $_.path})
$before=@{}
foreach($path in $protected){$before[$path]=[ordered]@{sha=(Get-FileHash -LiteralPath $path).Hash;write=(Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks}}
Check 'PlanOnly is read-only and separates design, generation and publication' {
    $entries=@([IO.Directory]::EnumerateFileSystemEntries($TestRoot))
    $plan=Read-S1ReleasePackagingPlan $Root $R2EvidenceRoot $R3EvidenceRoot
    if($plan.designGenerationFlag -or !$plan.r4BGenerationAuthorized -or $plan.binaryReleaseApproved -or $plan.publicDistributionAllowed -or $plan.packagingRevision -ceq $plan.productSourceRevision){throw 'Approval or revision confusion'}
    Assert-S1InputSet $entries @([IO.Directory]::EnumerateFileSystemEntries($TestRoot))
}
$good=Join-Path $TestRoot good
Check 'New private candidate and independent full receipt acceptance' {
    $receipt=New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $good
    if($receipt.files -ne 45 -or $receipt.PEMatchesR2 -ne 18 -or $receipt.unchangedFromR3 -ne 43 -or $receipt.binaryReleaseApproved -or $receipt.publicDistributionAllowed){throw 'Private acceptance'}
    $null=Test-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $good
}
Check 'ZIP, expansion, document hashes, references and marker match the fixed release design' {
    $null=Test-S1ReleaseCandidateZip $Root $R2EvidenceRoot $R3EvidenceRoot (Join-Path $good $profile.zipName)
    $null=Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot (Join-Path $good expanded)
    if([IO.File]::ReadAllText((Join-Path $good expanded/clipss2.portable)) -cne "clipss2-portable-v1`n"){throw 'Marker'}
}
foreach($change in @('public','binary','legal','scope','generation','fallback')){
    $path=Join-Path $TestRoot ('authorization-'+$change+'.json')
    $text=switch($change){
        public {$authText.Replace('"publicDistributionAllowed": false','"publicDistributionAllowed": true')}
        binary {$authText.Replace('"binaryReleaseApproved": false','"binaryReleaseApproved": true')}
        legal {$authText.Replace('"legalApprovalGranted": false','"legalApprovalGranted": true')}
        scope {$authText.Replace('R4-B-PRIVATE-CANDIDATE-ONLY','PUBLIC-RELEASE')}
        generation {$authText.Replace('"r4BGenerationAuthorized": true','"r4BGenerationAuthorized": false')}
        fallback {$authText.Replace('"legacyPrivateFallbackAllowed": false','"legacyPrivateFallbackAllowed": true')}
    }
    if($text -ceq $authText){throw 'Fixture did not mutate'}
    [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    $output=Join-Path $TestRoot ('rejected-'+$change)
    Refuse "Generation authorization rejects $change" {New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $output $path} 'S1:RawIdentity'
    if(Test-Path -LiteralPath $output){throw 'Rejected authorization wrote output'}
}
Refuse 'R4-A profile is not a generation authorization' {Read-S1ReleaseGenerationAuthorization $Root packaging/public/profile-s1-binary-release-v1.json} 'S1:RawIdentity'
Refuse 'R3 private profile cannot serve as authorization or fallback' {Read-S1ReleaseGenerationAuthorization $Root packaging/public/profile-s1-binary-private-v1.json} 'S1:RawIdentity'
Refuse 'Missing authorization fails closed' {New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot (Join-Path $TestRoot no-authorization) (Join-Path $TestRoot absent.json)} 'S1:FileMissing'
if(Test-Path -LiteralPath (Join-Path $TestRoot no-authorization)){throw 'Missing authority created output'}
$missingRoot=Join-Path $TestRoot missing-design
[void][IO.Directory]::CreateDirectory((Join-Path $missingRoot packaging/public))
[IO.File]::Copy($authPath,(Join-Path $missingRoot packaging/public/authorization-s1-r4b-private-v1.json),$false)
[IO.File]::Copy((Join-Path $Root packaging/public/profile-s1-binary-private-v1.json),(Join-Path $missingRoot packaging/public/profile-s1-binary-private-v1.json),$false)
Refuse 'Missing release design does not fall back to existing private profile' {Read-S1ReleaseGenerationAuthorization $missingRoot} 'S1:FileMissing'
$fakeR2=Join-Path $TestRoot fake-r2
[void][IO.Directory]::CreateDirectory($fakeR2)
[IO.File]::Copy((Join-Path $R2EvidenceRoot identity.json),(Join-Path $fakeR2 identity.json),$false)
[IO.File]::WriteAllText((Join-Path $fakeR2 result.json),'{}')
Refuse 'R2 evidence substitution refused' {Read-S1ReleasePackagingPlan $Root $fakeR2 $R3EvidenceRoot} 'S1:RawIdentity'
$fakeR3=Join-Path $TestRoot fake-r3
[void][IO.Directory]::CreateDirectory($fakeR3)
[IO.File]::WriteAllText((Join-Path $fakeR3 result.json),'{}')
Refuse 'R3 evidence substitution refused' {Read-S1ReleasePackagingPlan $Root $R2EvidenceRoot $fakeR3} 'S1:RawIdentity'
$empty=Join-Path $TestRoot existing
[void][IO.Directory]::CreateDirectory($empty)
Refuse 'Existing empty output cannot be adopted' {New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $empty} 'S1:R4B:ExistingOutput'
[IO.File]::WriteAllText((Join-Path $empty sentinel),'preserve')
Refuse 'Existing unknown contents cannot be overwritten' {New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $empty} 'S1:R4B:ExistingOutput'
Check 'Unknown contents retained' {if([IO.File]::ReadAllText((Join-Path $empty sentinel)) -cne 'preserve'){throw 'Overwrite'}}
Refuse 'Output outside authorization containers refused' {New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot (Join-Path $Root build/forbidden-r4b)} 'S1:R4B:OutputScope'
foreach($extra in @('settings.conf','product.pdb','library.lib','sample.png','audit.log','PRIVATE/notes.md','authorization.json')){
    $fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot ('extra-'+$extra.Replace('/','-')))
    $path=Join-Path $fixture $extra
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path,'synthetic')
    Refuse "Payload rejects extra $extra" {Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot $fixture} 'S1:ScopeFiles'
}
foreach($name in @('clipss2.exe','licenses/jpegli/PATENTS','README.md','THIRD-PARTY-NOTICES.md')){
    $fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot ('modified-'+$name.Replace('/','-')))
    Mutate-Byte (Join-Path $fixture $name)
    Refuse "Payload rejects modified $name" {Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot $fixture} 'S1:RawIdentity'
}
$fixture=Copy-Tree (Join-Path $good payload) (Join-Path $TestRoot reparse)
$target=Join-Path $TestRoot owned-junction-target
[void][IO.Directory]::CreateDirectory($target)
$null=New-Item -ItemType Junction -Path (Join-Path $fixture link) -Target $target
Refuse 'Reparse payload refused' {Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot $fixture} 'S1:ReparsePoint'
Refuse 'Reparse output refused' {New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot (Join-Path $fixture link/candidate)} 'S1:ReparsePoint'
Refuse 'Old R3 ZIP rejected as a release candidate' {Test-S1ReleaseCandidateZip $Root $R2EvidenceRoot $R3EvidenceRoot (Join-Path $R3EvidenceRoot candidate/clipss2-s1-v2-93545d4-private-candidate.zip)} 'S1:Binary:Zip'
foreach($change in @('extra','traversal','duplicate','content')){
    $path=Join-Path $TestRoot ('invalid-'+$change+'.zip')
    $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew)
    $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try{
        for($i=0;$i -lt $profile.files.Count;$i++){
            $file=$profile.files[$i];$name=$file.path
            if($i -eq 0 -and $change -eq 'traversal'){$name='../escape'}
            if($i -eq 0 -and $change -eq 'duplicate'){$name=$profile.files[1].path.ToUpperInvariant()}
            $entry=$archive.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime=[DateTimeOffset]'2026-10-23T09:00:00+09:00';$entry.ExternalAttributes=32
            $bytes=[IO.File]::ReadAllBytes((Join-Path $good ('payload/'+$file.path)))
            if($change -eq 'content' -and $file.path -ceq 'README.md'){$bytes[0]=$bytes[0] -bxor 1}
            $out=$entry.Open();try{$out.Write($bytes,0,$bytes.Length)}finally{$out.Dispose()}
        }
        if($change -eq 'extra'){$null=$archive.CreateEntry('settings.conf')}
    }finally{$archive.Dispose();$stream.Dispose()}
    Refuse "ZIP rejects $change" {Test-S1ReleaseCandidateZip $Root $R2EvidenceRoot $R3EvidenceRoot $path} 'S1:(Binary:Zip|RelativePath)'
}
foreach($change in @('receipt-public','owner-public','receipt-revision')){
    $fixture=Copy-Tree $good (Join-Path $TestRoot $change)
    $name=if($change -eq 'owner-public'){'owner.json'}else{'acceptance.json'}
    $path=Join-Path $fixture $name
    $text=[IO.File]::ReadAllText($path)
    if($change -eq 'receipt-revision'){$text=$text.Replace('73c593690b46b018975f3b914da97ab334ad0e35','93545d458da33cef397fb77458336ea06339dcb6')}
    else{$text=$text.Replace('"publicDistributionAllowed": false','"publicDistributionAllowed": true')}
    [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    Refuse "Full acceptance rejects $change" {Test-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $fixture} 'S1:Release(Owner|Acceptance)Record'
}
Check 'Failure after ZIP writing preserves partial data without accepted receipt' {
    $module=Get-Module S1BinaryReleasePackage
    $original=& $module {(Get-Command Read-R4BArchive).ScriptBlock}
    $partial=Join-Path $TestRoot partial
    try{
        & $module {function script:Read-R4BArchive {throw 'S1:R4B:InjectedAdmissionFailure'}}
        Refuse 'Injected archive admission failure stops generation' {New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $partial} 'S1:R4B:InjectedAdmissionFailure'
    }finally{& $module {param($Body) Set-Item Function:script:Read-R4BArchive $Body} $original}
    if(!(Test-Path -LiteralPath (Join-Path $partial $profile.zipName)) -or (Test-Path -LiteralPath (Join-Path $partial acceptance.json)) -or (Test-Path -LiteralPath (Join-Path $partial expanded))){throw 'Partial output admission'}
    Refuse 'Partial output cannot be admitted as a candidate' {Test-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $partial} 'S1:ScopeFiles'
}
Check 'All protected input bytes and write times remain unchanged' {
    foreach($path in $before.Keys){if((Get-FileHash -LiteralPath $path).Hash -cne $before[$path].sha -or (Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks -ne $before[$path].write){throw 'Protected input changed'}}
}
$report=[ordered]@{passed=$results.Count;failed=0;scope='R4-B private Packaging only';tests=@($results.ToArray());binaryReleaseApproved=$false;publicDistributionAllowed=$false}
[IO.File]::WriteAllText((Join-Path $TestRoot results.json),($report|ConvertTo-Json -Depth 16).Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]$report|Select-Object passed,failed,scope,binaryReleaseApproved,publicDistributionAllowed|ConvertTo-Json
