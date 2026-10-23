#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Phase 3-D v4: new contract; the v3 reader/writer and artifacts remain immutable.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
Import-Module (Join-Path $PSScriptRoot S1SourceNotice.psm1)
$script:ProfileSha='37369A41BEB90BE0A413FA1C4492CB5DF8836F470DA1EA8FB431F0DA1E29B78B'
$script:AuthSha='B3D463E3C0E759D0221CC49C79BE684AC2B3B11E3A771BE95D18FB60211C6766'
$old=Join-Path $PSScriptRoot S1BinarySdk2Package.psm1
Assert-S1Identity $old 21777 'E59850DF67428E3DA8A46894A9455AFDC974A8C5FF1AEF34E03E683FCA134CCE'
$script:Legacy=Import-Module $old -PassThru
# Parameterized 46-entry archive/path/CreateNew helpers accept the actual v4 profile.
# They do not call an old Plan/Create operation or impersonate an old profile.
function Get-Sdk4Absolute($Path){& $script:Legacy {param($P) Get-Sdk2Absolute $P} $Path}
function New-Sdk4Directory($Path){& $script:Legacy {param($P) New-Sdk2Directory $P} $Path}
function Write-Sdk4Json($Path,$Value){& $script:Legacy {param($P,$V) Write-Sdk2Json $P $V} $Path $Value}
function Write-Sdk4File($Path,[byte[]]$Bytes){& $script:Legacy {param($P,$B) Write-Sdk2File $P $B} $Path $Bytes}
function Get-Sdk4Hash([byte[]]$Bytes){& $script:Legacy {param($B) Get-Sdk2Hash $B} $Bytes}
function Read-Sdk4Archive($Path,$Profile){& $script:Legacy {param($P,$V) Read-Sdk2Archive $P $V} $Path $Profile}
function Assert-Sdk4Tree($Path,$Profile){& $script:Legacy {param($P,$V) Assert-Sdk2Tree $P $V} $Path $Profile}

function Get-Sdk4Section($Text,[int]$Number){
    $matches=[regex]::Matches($Text,('(?ms)^'+$Number+'\. .*?(?=^\d\. |\z)'))
    if($matches.Count -ne 1){throw 'S1:Sdk4:SectionStructure'}
    return $matches[0].Value
}
function Assert-Sdk4Terms($NewText,$OldText,$Revision){
    if($NewText.Contains("`r") -or $OldText.Contains("`r")){throw 'S1:Sdk4:TermsLineEndings'}
    if([regex]::Matches($NewText,'(?m)^[1-7]\. ').Count -ne 7){throw 'S1:Sdk4:SectionStructure'}
    # Only the version line can change outside the two approved sections.
    $first='1. Scope and separate licenses'
    $newPrefix=$NewText.Substring(0,$NewText.IndexOf($first,[StringComparison]::Ordinal))
    $oldPrefix=$OldText.Substring(0,$OldText.IndexOf($first,[StringComparison]::Ordinal))
    Assert-S1NoticeValue $newPrefix ($oldPrefix.Replace('Version 1 — 2026-10-23','Version 2 — 2026-10-23')) Sdk4TermsHeader
    foreach($n in @(1,2,3,5,7)){Assert-S1NoticeValue (Get-Sdk4Section $NewText $n) (Get-Sdk4Section $OldText $n) Sdk4TermsInheritance}
    foreach($n in @(4,6)){
        $body=(Get-Sdk4Section $NewText $n).TrimEnd()+"`n"
        $hash=Get-Sdk4Hash ([Text.UTF8Encoding]::new($false).GetBytes($body))
        Assert-S1NoticeValue $hash $Revision[('section'+$n+'Sha256')] Sdk4ApprovedSection
    }
}
function Read-S1Sdk4Authorization($Root,[string]$AuthorizationPath){
    if(!$AuthorizationPath){$AuthorizationPath=Resolve-S1Path $Root 'packaging/public/authorization-s1-sdk2-v4-private-v1.json'}
    elseif(![IO.Path]::IsPathFullyQualified($AuthorizationPath)){$AuthorizationPath=Resolve-S1Path $Root $AuthorizationPath}
    Assert-S1Identity $AuthorizationPath 1779 $script:AuthSha
    $auth=Read-S1StrictJson $AuthorizationPath
    Assert-S1NoticeValue $auth.approvalScope 'PHASE3D-SDK2-V4-PRIVATE-CANDIDATE-ONLY'
    Assert-S1NoticeValue $auth.candidateGenerationAuthorized $true
    Assert-S1NoticeValue $auth.ownerTermsTextApproved $true
    Assert-S1NoticeValue $auth.ownerApprovedSections @(4,6)
    foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','githubReleaseAllowed','signingAllowed','legalApprovalGranted','reuseAllowed','legacyFallbackAllowed')){Assert-S1NoticeValue $auth[$key] $false}
    foreach($b in @($auth.releaseProfile,$auth.legacyImplementation,$auth.legacyCLI)){Assert-S1Identity (Resolve-S1Path $Root $b.path) $b.size $b.sha256}
    return $auth
}
function Read-S1Sdk4State($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$V3EvidenceRoot,[string]$AuthorizationPath,[switch]$ForCreation){
    $Root=Get-Sdk4Absolute $Root
    $auth=Read-S1Sdk4Authorization $Root $AuthorizationPath
    $profilePath=Resolve-S1Path $Root $auth.releaseProfile.path
    Assert-S1Identity $profilePath 31780 $script:ProfileSha
    $p=Read-S1StrictJson $profilePath
    foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','legalApprovalGranted','sourceApprovalInherited','privateApprovalInherited')){Assert-S1NoticeValue $p[$key] $false}
    Assert-S1NoticeValue $p.profileId 'clipss2-s1-binary-release-v4-sdk2'
    Assert-S1NoticeValue $p.microsoftStatus.skillVersion '1.3.1'
    Assert-S1NoticeValue $p.microsoftStatus.MSVC1 'INTERPRETED-APPROVED'
    Assert-S1NoticeValue $p.microsoftStatus.TOOLS1 'CLEAR'
    Assert-S1NoticeValue $p.microsoftStatus.SDK2 'CLEAR IF'
    Assert-S1NoticeValue $p.microsoftStatus.G3 'NOT AUTHORIZED'
    Assert-S1NoticeValue $p.microsoftStatus.ownerTermsTextApproved $true
    Assert-S1NoticeValue $p.microsoftStatus.ownerApprovedTermsSections @(4,6)
    foreach($key in @('equivalentProtectionVerified','recipientAgreementEstablished','downstreamFulfillmentVerified','ownerResidualRiskAccepted')){Assert-S1NoticeValue $p.microsoftStatus[$key] $false}
    Assert-S1NoticeValue $p.sourceRevision $auth.productSourceRevision
    Assert-S1NoticeValue $p.packagingBaseRevision $auth.packagingHead
    # Preserve v3's own fixed contracts and admission. No call to the old creation gate.
    foreach($b in $p.v3Bindings){Assert-S1Identity (Resolve-S1Path $V3EvidenceRoot $b.path) $b.size $b.sha256}
    $null=& $script:Legacy {param($R,$E2,$E3,$EB,$ED,$E3C) Test-S1Sdk2Candidate $R $E2 $E3 $EB $ED (Resolve-S1Path $E3C 'candidate')} $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot
    Assert-S1Identity (Resolve-S1Path $Root $p.parentProfile.path) $p.parentProfile.size $p.parentProfile.sha256
    $parent=Read-S1StrictJson (Resolve-S1Path $Root $p.parentProfile.path)
    foreach($key in @('sourceRevision','branch','preset','sourceScopeId','sourceInputs','sourceNoticeBindings','r2Bindings','r3Bindings','r4bBindings','r4dBindings','runtime','stlLicenseOrigin','components','PECount','licenseCount','zipName')){Assert-S1NoticeValue $p[$key] $parent[$key] Sdk4Inheritance}
    Assert-S1NoticeValue $p.priorDecisionRecord $parent.decisionRecord Sdk4PriorDecision
    foreach($b in @($p.decisionRecord,$p.priorDecisionRecord,$p.releasePublication.priorReleaseNotes)){Assert-S1Identity (Resolve-S1Path $Root $b.path) $b.size $b.sha256}
    if($p.files.Count -ne 46 -or $parent.files.Count -ne 46){throw 'S1:Sdk4:FileCount'}
    Assert-S1InputSet @($parent.files.path) @($p.files.path)
    foreach($file in $p.files){
        $prior=@($parent.files|Where-Object path -CEQ $file.path)[0]
        if($file.path -ceq 'MICROSOFT-COMPONENT-TERMS.txt'){
            Assert-S1NoticeValue $file.source 'packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt'
            Assert-S1NoticeValue $file.kind $prior.kind
            $newText=[IO.File]::ReadAllText((Resolve-S1Path $Root $file.source))
            $oldText=[IO.File]::ReadAllText((Resolve-S1Path $Root $prior.source))
            Assert-Sdk4Terms $newText $oldText $p.termsRevision
        }else{Assert-S1NoticeValue $file $prior Sdk4PayloadInheritance}
        Assert-S1Identity (Resolve-S1Path $Root $file.source) $file.size $file.sha256
    }
    $priorReceipt=Read-S1StrictJson (Resolve-S1Path $V3EvidenceRoot 'candidate/acceptance.json')
    $expected=@($parent.files|ForEach-Object {[ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256}})
    Assert-S1NoticeValue $priorReceipt.inventory $expected Sdk4ParentInventory
    Assert-S1NoticeValue $priorReceipt.binaryReleaseApproved $false
    Assert-S1NoticeValue $priorReceipt.publicDistributionAllowed $false
    # R2 identity is already admitted by the unchanged parent reader; compare the v4 PE inventory.
    $identity=Read-S1StrictJson (Resolve-S1Path $R2EvidenceRoot identity.json)
    $pe=@($p.files|Where-Object kind -CEQ PE)
    Assert-S1InputSet @($pe.path) @($identity.identities.name)
    foreach($file in $pe){$r=@($identity.identities|Where-Object name -CEQ $file.path)[0];Assert-S1NoticeValue $file.size $r.size;Assert-S1NoticeValue $file.sha256 $r.sha256}
    foreach($file in @($p.files|Where-Object {$_.path -cin @('README.md','THIRD-PARTY-NOTICES.md')})){
        $text=[IO.File]::ReadAllText((Resolve-S1Path $Root $file.source))
        foreach($link in [regex]::Matches($text,'\]\(([^)]+)\)')){
            $target=$link.Groups[1].Value
            if($target.StartsWith('https://',[StringComparison]::Ordinal)){continue}
            Assert-S1RelativePath $target
            if($target -cnotin $p.files.path){throw 'S1:Sdk4:DocumentReference'}
        }
    }
    if($ForCreation){
        $head=& git --no-optional-locks -C $Root rev-parse HEAD
        if($LASTEXITCODE -ne 0 -or $head -cne $auth.packagingHead){throw 'S1:Sdk4:PackagingHead'}
        $branch=& git --no-optional-locks -C $Root branch --show-current
        if($LASTEXITCODE -ne 0 -or $branch -cne $auth.branch){throw 'S1:Sdk4:Branch'}
        $top=& git --no-optional-locks -C $Root rev-parse --show-toplevel
        if($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($top) -ine $Root){throw 'S1:Sdk4:GitRoot'}
    }
    return [pscustomobject]@{profile=$p;authorization=$auth;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Read-S1Sdk4Plan($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$V3EvidenceRoot,[string]$AuthorizationPath){
    $state=Read-S1Sdk4State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $AuthorizationPath -ForCreation
    return [pscustomobject]@{result='PRIVATE_SDK2_V4_GENERATION_AUTHORIZED';files=46;PEMatchesR2=18;unchangedFromV3=45;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false;productSourceRevision=$state.profile.sourceRevision;packagingRevision=$state.authorization.packagingHead;zipName=$state.profile.zipName}
}
function Test-S1Sdk4Tree($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$V3EvidenceRoot,$PackageRoot,[string]$AuthorizationPath){
    $state=Read-S1Sdk4State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $AuthorizationPath
    $PackageRoot=Get-Sdk4Absolute $PackageRoot
    Assert-Sdk4Tree $PackageRoot $state.profile
    return [pscustomobject]@{result='PASS';files=46;PEMatchesR2=18;unchangedFromV3=45;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Test-S1Sdk4Zip($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$V3EvidenceRoot,$ZipPath,[string]$AuthorizationPath){
    $state=Read-S1Sdk4State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $AuthorizationPath
    $zip=Read-Sdk4Archive $ZipPath $state.profile
    return [pscustomobject]@{result='PASS';files=46;size=$zip.size;sha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Get-Sdk4Owner($State){return [ordered]@{state='OWNED_PRIVATE_SDK2_V4_CANDIDATE';reuseAllowed=$false;acceptanceRequiresSeparateReceipt=$true;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false;authorizationSha256=$script:AuthSha;profileSha256=$script:ProfileSha;productSourceRevision=$State.profile.sourceRevision;packagingRevision=$State.authorization.packagingHead}}
function Get-Sdk4Receipt($Root,$State,$Zip){
    $inventory=@($State.profile.files|ForEach-Object {[ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256}})
    $text=(($inventory|ForEach-Object {"{0}`t{1}`t{2}" -f $_.path,$_.size,$_.sha256}) -join "`n")+"`n"
    $implementation=@(foreach($relative in @('packaging/S1BinarySdk2V4Package.psm1','tools/package_s1_binary_sdk2_v4.ps1')){$path=Resolve-S1Path $Root $relative;[ordered]@{path=$relative;size=[IO.FileInfo]::new($path).Length;sha256=(Get-Sdk4Hash ([IO.File]::ReadAllBytes($path)))}})
    return [ordered]@{
        result='PASS';verdict='PASS WITH LIMITATIONS';purpose='PHASE3D-SDK2-V4-PRIVATE-CANDIDATE-ONLY'
        productSourceRevision=$State.profile.sourceRevision;packagingRevision=$State.authorization.packagingHead;implementation=$implementation
        authorizationSha256=$script:AuthSha;profileSha256=$script:ProfileSha;candidateGenerationAuthorized=$true
        binaryReleaseApproved=$false;publicDistributionAllowed=$false;thirdPartyTransferAllowed=$false;legalApprovalGranted=$false
        signed=$false;published=$false;tagCreated=$false;files=46;PEMatchesR2=18;unchangedFromV3=45;thirdPartyOriginals=23
        releaseProposal=$State.profile.releaseProposal;termsRevision=$State.profile.termsRevision
        zip=[ordered]@{path=$State.profile.zipName;size=$Zip.size;sha256=$Zip.sha256}
        inventoryEncoding='UTF-8; path TAB decimal-size TAB uppercase-SHA256 LF; final LF';inventorySha256=(Get-Sdk4Hash ([Text.UTF8Encoding]::new($false).GetBytes($text)));inventory=$inventory
        G1='VERIFIED';MSVC1='INTERPRETED-APPROVED';TOOLS1='CLEAR';SDK1='CONDITIONAL';SDK2='CLEAR IF';G3='NOT AUTHORIZED'
        ownerWordingSelected=$true;ownerTermsTextApproved=$true;ownerApprovedSections=@(4,6);equivalentProtectionVerified=$false;recipientAgreementEstablished=$false
        packageTermsPresent=$true;releasePagePublished=$false;downstreamFulfillmentVerified=$false;ownerResidualRiskAccepted=$false;reuseAllowed=$false
        provenance='Unsigned local integrity measurements; not license grant or publication approval'
    }
}
function Test-S1Sdk4Candidate($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$V3EvidenceRoot,$CandidateRoot,[string]$AuthorizationPath){
    $state=Read-S1Sdk4State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $AuthorizationPath
    $CandidateRoot=Get-Sdk4Absolute $CandidateRoot
    $paths=@('owner.json','acceptance.json',$state.profile.zipName)
    foreach($prefix in @('payload/','expanded/')){$paths+=@($state.profile.files|ForEach-Object {$prefix+$_.path})}
    Assert-S1NoticeTree $CandidateRoot $paths
    Assert-S1NoticeValue (Read-S1StrictJson (Resolve-S1Path $CandidateRoot owner.json)) (Get-Sdk4Owner $state) Sdk4Owner
    foreach($dir in @('payload','expanded')){Assert-Sdk4Tree (Join-Path $CandidateRoot $dir) $state.profile}
    $zip=Read-Sdk4Archive (Resolve-S1Path $CandidateRoot $state.profile.zipName) $state.profile
    Assert-S1NoticeValue (Read-S1StrictJson (Resolve-S1Path $CandidateRoot acceptance.json)) (Get-Sdk4Receipt $Root $state $zip) Sdk4Receipt
    return [pscustomobject]@{result='PASS';scope='PRIVATE_SDK2_V4_CANDIDATE_ONLY';files=46;PEMatchesR2=18;unchangedFromV3=45;size=$zip.size;sha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function New-S1Sdk4Candidate($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$V3EvidenceRoot,$OutputRoot,[string]$AuthorizationPath){
    $Root=Get-Sdk4Absolute $Root;$OutputRoot=Get-Sdk4Absolute $OutputRoot
    if(Test-Path -LiteralPath $OutputRoot){throw 'S1:Sdk4:ExistingOutput'}
    $state=Read-S1Sdk4State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $AuthorizationPath -ForCreation
    $p=$state.profile
    $relative=[IO.Path]::GetRelativePath($Root,$OutputRoot).Replace([char]92,[char]47)
    Assert-S1RelativePath $relative
    if($relative.Split('/').Count -lt 3 -or !@($state.authorization.outputPrefixes|Where-Object {$relative.StartsWith($_,[StringComparison]::Ordinal)}).Count){throw 'S1:Sdk4:OutputScope'}
    New-Sdk4Directory $OutputRoot
    Write-Sdk4Json (Join-Path $OutputRoot owner.json) (Get-Sdk4Owner $state)
    $payload=Join-Path $OutputRoot payload;New-Sdk4Directory $payload
    foreach($file in $p.files){
        $dest=Resolve-S1Path $payload $file.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        $source=if($file.path -ceq 'MICROSOFT-COMPONENT-TERMS.txt'){Resolve-S1Path $Root $file.source}else{Resolve-S1Path $V3EvidenceRoot ('candidate/expanded/'+$file.path)}
        [IO.File]::Copy($source,$dest,$false)
    }
    Assert-Sdk4Tree $payload $p
    $doc=[Text.Json.JsonDocument]::Parse([IO.File]::ReadAllText((Resolve-S1Path $Root $state.authorization.releaseProfile.path)))
    try{$date=[DateTimeOffset]::Parse($doc.RootElement.GetProperty('zipEntryDate').GetString(),[Globalization.CultureInfo]::InvariantCulture)}finally{$doc.Dispose()}
    $zipPath=Join-Path $OutputRoot $p.zipName
    $stream=[IO.File]::Open($zipPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try{foreach($file in $p.files){
        $entry=$archive.CreateEntry($file.path,[IO.Compression.CompressionLevel]::Optimal);$entry.LastWriteTime=$date;$entry.ExternalAttributes=32
        $out=$entry.Open();$input=[IO.File]::OpenRead((Resolve-S1Path $payload $file.path))
        try{$input.CopyTo($out)}finally{$input.Dispose();$out.Dispose()}
    }}finally{$archive.Dispose();$stream.Dispose()}
    $zip=Read-Sdk4Archive $zipPath $p
    $expanded=Join-Path $OutputRoot expanded;New-Sdk4Directory $expanded
    foreach($file in $p.files){$dest=Resolve-S1Path $expanded $file.path;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest));Write-Sdk4File $dest $zip.contents[$file.path]}
    Assert-Sdk4Tree $expanded $p
    $null=Read-S1Sdk4State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $V3EvidenceRoot $AuthorizationPath -ForCreation
    $receipt=Get-Sdk4Receipt $Root $state $zip
    Write-Sdk4Json (Join-Path $OutputRoot acceptance.json) $receipt
    return $receipt
}
Export-ModuleMember -Function Read-S1Sdk4Authorization,Read-S1Sdk4Plan,Test-S1Sdk4Tree,Test-S1Sdk4Zip,Test-S1Sdk4Candidate,New-S1Sdk4Candidate
