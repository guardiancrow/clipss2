#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# R4-D generation. No prior profile, reader, writer or authorization is replaced.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
Import-Module (Join-Path $PSScriptRoot S1SourceNotice.psm1)
$script:ProfileSha='E5F7422666C39D3F7A67D37445A0DCCD5B65A55C4943E2F3C8C6830AC89AAC10'
$script:AuthSha='CFD6E7DE80F60C67D555408EFB67F7889CF5486A84CBCD7242BDC9B2A6231B6E'
$old=Join-Path $PSScriptRoot S1BinaryReleasePackage.psm1
Assert-S1Identity $old 13969 'FA76B8A278891990018D61138E2ED9B55A54828A14F9678F8383962952D06B86'
$script:Legacy=Import-Module $old -PassThru
# These are the existing parameterized path, CreateNew and archive primitives.
function Get-FinalAbsolute($Path){& $script:Legacy {param($P) Get-R4BAbsolute $P} $Path}
function New-FinalDirectory($Path){& $script:Legacy {param($P) New-R4BDirectory $P} $Path}
function Write-FinalJson($Path,$Value){& $script:Legacy {param($P,$V) Write-R4BJson $P $V} $Path $Value}
function Write-FinalFile($Path,[byte[]]$Bytes){& $script:Legacy {param($P,$B) Write-R4BFile $P $B} $Path $Bytes}
function Get-FinalHash([byte[]]$Bytes){& $script:Legacy {param($B) Get-R4BHash $B} $Bytes}
function Read-FinalArchive($Path,$Profile){& $script:Legacy {param($P,$V) Read-R4BArchive $P $V} $Path $Profile}

function Read-S1FinalAuthorization($Root,[string]$AuthorizationPath){
    if(!$AuthorizationPath){$AuthorizationPath=Resolve-S1Path $Root 'packaging/public/authorization-s1-r4d-final-private-v1.json'}
    elseif(![IO.Path]::IsPathFullyQualified($AuthorizationPath)){$AuthorizationPath=Resolve-S1Path $Root $AuthorizationPath}
    Assert-S1Identity $AuthorizationPath 1600 $script:AuthSha
    $auth=Read-S1StrictJson $AuthorizationPath
    Assert-S1NoticeValue $auth.approvalScope 'R4-D-PRIVATE-FINAL-CANDIDATE-ONLY'
    Assert-S1NoticeValue $auth.candidateGenerationAuthorized $true
    foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','githubReleaseAllowed','signingAllowed','legalApprovalGranted','reuseAllowed','legacyFallbackAllowed')){Assert-S1NoticeValue $auth[$key] $false}
    foreach($binding in @($auth.releaseProfile,$auth.legacyImplementation,$auth.legacyCLI)){
        Assert-S1Identity (Resolve-S1Path $Root $binding.path) $binding.size $binding.sha256
    }
    return $auth
}
function Read-S1FinalState($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,[string]$AuthorizationPath,[switch]$ForCreation){
    $Root=Get-FinalAbsolute $Root
    $auth=Read-S1FinalAuthorization $Root $AuthorizationPath
    $profilePath=Resolve-S1Path $Root $auth.releaseProfile.path
    Assert-S1Identity $profilePath 29265 $script:ProfileSha
    $p=Read-S1StrictJson $profilePath
    foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','legalApprovalGranted','sourceApprovalInherited','privateApprovalInherited')){Assert-S1NoticeValue $p[$key] $false}
    Assert-S1NoticeValue $p.microsoftStatus.MS002L 'NEEDS CLARIFICATION'
    Assert-S1NoticeValue $p.microsoftStatus.downstreamFulfillmentVerified $false
    Assert-S1NoticeValue $p.microsoftStatus.ownerResidualRiskAccepted $false
    Assert-S1NoticeValue $p.sourceRevision $auth.productSourceRevision
    Assert-S1NoticeValue $p.packagingBaseRevision $auth.packagingHead
    # Reuse the existing R4-A evidence validation unchanged. R4-B receipt/ZIP
    # bytes are then bound to that accepted parent, without repeating PE audits.
    $null=& $script:Legacy {param($R,$E2,$E3) Read-S1BinaryReleasePlan $R $E2 $E3} $Root $R2EvidenceRoot $R3EvidenceRoot
    Assert-S1Identity (Resolve-S1Path $Root $p.parentProfile.path) $p.parentProfile.size $p.parentProfile.sha256
    $parent=Read-S1StrictJson (Resolve-S1Path $Root $p.parentProfile.path)
    foreach($key in @('sourceRevision','branch','preset','sourceScopeId','sourceInputs','sourceNoticeBindings','r2Bindings','r3Bindings','runtime','stlLicenseOrigin','components','PECount','licenseCount')){Assert-S1NoticeValue $p[$key] $parent[$key] FinalInheritance}
    Assert-S1InputSet @($parent.files.path) @($p.files.path)
    foreach($file in $p.files){
        $prior=@($parent.files|Where-Object path -CEQ $file.path)[0]
        if($file.path -ceq 'README.md'){Assert-S1NoticeValue $file.source 'packaging/binary/README-portable-s1-release-v2.md';Assert-S1NoticeValue $file.kind $prior.kind}
        elseif($file.path -ceq 'THIRD-PARTY-NOTICES.md'){Assert-S1NoticeValue $file.source 'packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v2.md';Assert-S1NoticeValue $file.kind $prior.kind}
        else{Assert-S1NoticeValue $file $prior FinalPayloadInheritance}
        Assert-S1Identity (Resolve-S1Path $Root $file.source) $file.size $file.sha256
    }
    Assert-S1Identity (Resolve-S1Path $Root $p.decisionRecord.path) $p.decisionRecord.size $p.decisionRecord.sha256
    foreach($binding in $p.r4bBindings){Assert-S1Identity (Resolve-S1Path $R4BEvidenceRoot $binding.path) $binding.size $binding.sha256}
    $priorReceipt=Read-S1StrictJson (Resolve-S1Path $R4BEvidenceRoot 'candidate/acceptance.json')
    $expected=@($parent.files|ForEach-Object {[ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256}})
    Assert-S1NoticeValue $priorReceipt.inventory $expected FinalParentInventory
    Assert-S1NoticeValue $priorReceipt.binaryReleaseApproved $false
    Assert-S1NoticeValue $priorReceipt.publicDistributionAllowed $false
    Assert-S1NoticeValue $priorReceipt.productSourceRevision $p.sourceRevision
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
            if($target -cnotin $p.files.path){throw 'S1:Final:DocumentReference'}
        }
    }
    if($ForCreation){
        $head=& git --no-optional-locks -C $Root rev-parse HEAD
        if($LASTEXITCODE -ne 0 -or $head -cne $auth.packagingHead){throw 'S1:Final:PackagingHead'}
        $branch=& git --no-optional-locks -C $Root branch --show-current
        if($LASTEXITCODE -ne 0 -or $branch -cne $auth.branch){throw 'S1:Final:Branch'}
        $top=& git --no-optional-locks -C $Root rev-parse --show-toplevel
        if($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($top) -ine $Root){throw 'S1:Final:GitRoot'}
    }
    return [pscustomobject]@{profile=$p;authorization=$auth;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Read-S1FinalPlan($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,[string]$AuthorizationPath){
    $state=Read-S1FinalState $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $AuthorizationPath -ForCreation
    return [pscustomobject]@{result='PRIVATE_FINAL_GENERATION_AUTHORIZED';files=45;PEMatchesR2=18;unchangedFromR4B=43;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false;productSourceRevision=$state.profile.sourceRevision;packagingRevision=$state.authorization.packagingHead;zipName=$state.profile.zipName}
}
function Test-S1FinalTree($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$PackageRoot,[string]$AuthorizationPath){
    $state=Read-S1FinalState $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $AuthorizationPath
    $PackageRoot=Get-FinalAbsolute $PackageRoot
    Assert-S1NoticeTree $PackageRoot @($state.profile.files.path)
    foreach($file in $state.profile.files){Assert-S1Identity (Resolve-S1Path $PackageRoot $file.path) $file.size $file.sha256}
    return [pscustomobject]@{result='PASS';files=45;PEMatchesR2=18;unchangedFromR4B=43;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Test-S1FinalZip($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$ZipPath,[string]$AuthorizationPath){
    $state=Read-S1FinalState $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $AuthorizationPath
    $zip=Read-FinalArchive $ZipPath $state.profile
    return [pscustomobject]@{result='PASS';files=45;size=$zip.size;sha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Get-FinalOwner($State){return [ordered]@{state='OWNED_PRIVATE_FINAL_CANDIDATE';reuseAllowed=$false;acceptanceRequiresSeparateReceipt=$true;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false;authorizationSha256=$script:AuthSha;profileSha256=$script:ProfileSha;productSourceRevision=$State.profile.sourceRevision;packagingRevision=$State.authorization.packagingHead}}
function Get-FinalReceipt($Root,$State,$Zip){
    $inventory=@($State.profile.files|ForEach-Object {[ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256}})
    $text=(($inventory|ForEach-Object {"{0}`t{1}`t{2}" -f $_.path,$_.size,$_.sha256}) -join "`n")+"`n"
    $implementation=@(foreach($relative in @('packaging/S1BinaryFinalPackage.psm1','tools/package_s1_binary_final.ps1')){$path=Resolve-S1Path $Root $relative;[ordered]@{path=$relative;size=[IO.FileInfo]::new($path).Length;sha256=(Get-FinalHash ([IO.File]::ReadAllBytes($path)))}})
    return [ordered]@{
        result='PASS';verdict='PASS WITH LIMITATIONS';purpose='R4-D-PRIVATE-FINAL-CANDIDATE-ONLY'
        productSourceRevision=$State.profile.sourceRevision;packagingRevision=$State.authorization.packagingHead;implementation=$implementation
        authorizationSha256=$script:AuthSha;profileSha256=$script:ProfileSha;candidateGenerationAuthorized=$true
        binaryReleaseApproved=$false;publicDistributionAllowed=$false;thirdPartyTransferAllowed=$false;legalApprovalGranted=$false
        signed=$false;published=$false;tagCreated=$false;files=45;PEMatchesR2=18;unchangedFromR4B=43;thirdPartyOriginals=23
        releaseProposal=$State.profile.releaseProposal
        zip=[ordered]@{path=$State.profile.zipName;size=$Zip.size;sha256=$Zip.sha256}
        inventoryEncoding='UTF-8; path TAB decimal-size TAB uppercase-SHA256 LF; final LF';inventorySha256=(Get-FinalHash ([Text.UTF8Encoding]::new($false).GetBytes($text)));inventory=$inventory
        MS002T='VERIFIED';MS002SDK='CONDITIONAL';MS002L='NEEDS CLARIFICATION';downstreamFulfillmentVerified=$false;ownerResidualRiskAccepted=$false;reuseAllowed=$false
        provenance='Unsigned local integrity measurements; not license grant or publication approval'
    }
}
function Test-S1FinalCandidate($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$CandidateRoot,[string]$AuthorizationPath){
    $state=Read-S1FinalState $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $AuthorizationPath
    $CandidateRoot=Get-FinalAbsolute $CandidateRoot
    $paths=@('owner.json','acceptance.json',$state.profile.zipName)
    foreach($prefix in @('payload/','expanded/')){$paths+=@($state.profile.files|ForEach-Object {$prefix+$_.path})}
    Assert-S1NoticeTree $CandidateRoot $paths
    Assert-S1NoticeValue (Read-S1StrictJson (Resolve-S1Path $CandidateRoot owner.json)) (Get-FinalOwner $state) FinalOwner
    foreach($dir in @('payload','expanded')){$null=Test-S1FinalTree $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot (Join-Path $CandidateRoot $dir) $AuthorizationPath}
    $zip=Read-FinalArchive (Resolve-S1Path $CandidateRoot $state.profile.zipName) $state.profile
    Assert-S1NoticeValue (Read-S1StrictJson (Resolve-S1Path $CandidateRoot acceptance.json)) (Get-FinalReceipt $Root $state $zip) FinalReceipt
    return [pscustomobject]@{result='PASS';scope='PRIVATE_FINAL_CANDIDATE_ONLY';files=45;PEMatchesR2=18;unchangedFromR4B=43;size=$zip.size;sha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function New-S1FinalCandidate($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$OutputRoot,[string]$AuthorizationPath){
    $Root=Get-FinalAbsolute $Root;$OutputRoot=Get-FinalAbsolute $OutputRoot
    if(Test-Path -LiteralPath $OutputRoot){throw 'S1:Final:ExistingOutput'}
    $state=Read-S1FinalState $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $AuthorizationPath -ForCreation
    $p=$state.profile
    $relative=[IO.Path]::GetRelativePath($Root,$OutputRoot).Replace([char]92,[char]47)
    Assert-S1RelativePath $relative
    if($relative.Split('/').Count -lt 3 -or !@($state.authorization.outputPrefixes|Where-Object {$relative.StartsWith($_,[StringComparison]::Ordinal)}).Count){throw 'S1:Final:OutputScope'}
    New-FinalDirectory $OutputRoot
    Write-FinalJson (Join-Path $OutputRoot owner.json) (Get-FinalOwner $state)
    $payload=Join-Path $OutputRoot payload;New-FinalDirectory $payload
    foreach($file in $p.files){$dest=Resolve-S1Path $payload $file.path;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest));[IO.File]::Copy((Resolve-S1Path $Root $file.source),$dest,$false)}
    $null=Test-S1FinalTree $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $payload $AuthorizationPath
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
    $zip=Read-FinalArchive $zipPath $p
    $expanded=Join-Path $OutputRoot expanded;New-FinalDirectory $expanded
    foreach($file in $p.files){$dest=Resolve-S1Path $expanded $file.path;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest));Write-FinalFile $dest $zip.contents[$file.path]}
    $null=Test-S1FinalTree $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $expanded $AuthorizationPath
    $null=Read-S1FinalState $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $AuthorizationPath -ForCreation
    $receipt=Get-FinalReceipt $Root $state $zip
    Write-FinalJson (Join-Path $OutputRoot acceptance.json) $receipt
    return $receipt
}
Export-ModuleMember -Function Read-S1FinalAuthorization,Read-S1FinalPlan,Test-S1FinalTree,Test-S1FinalZip,Test-S1FinalCandidate,New-S1FinalCandidate
