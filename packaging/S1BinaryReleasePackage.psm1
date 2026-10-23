#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# R4-B private generation envelope over the unchanged R4-A design.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
Import-Module (Join-Path $PSScriptRoot S1SourceNotice.psm1)
$script:AuthorizationRelative='packaging/public/authorization-s1-r4b-private-v1.json'
$script:AuthorizationSha='AF345EF61328C3A43657F36D8CD3DC07A7BF6F7F3644FB7ADF6DFD18122D4852'
$script:DesignSha='CD7F936F638FA634C8E335E93DB7D3566661B7F6E72C842206B620F9ECE4D29B'
# Reuse only the unchanged generic archive/path/CreateNew primitives. Never
# substitute a profile into the old reader/writer or change its SHA/HEAD checks.
$legacyPath=Join-Path $PSScriptRoot S1BinaryPackage.psm1
Assert-S1Identity $legacyPath ([IO.FileInfo]::new($legacyPath).Length) 'D37C0B9E6572FF3DBE50A2A2E1A776D9DCD0C2183F4EE5B757F2954105C8334B'
$script:Legacy=Import-Module $legacyPath -PassThru
$designReaderPath=Join-Path $PSScriptRoot S1BinaryReleasePlan.psm1
Assert-S1Identity $designReaderPath ([IO.FileInfo]::new($designReaderPath).Length) '9FF55A4671609859CD288A0FDC15925D5B8B62432799DACF09491984752962C4'
Import-Module $designReaderPath
function Get-R4BAbsolute($Path){ & $script:Legacy {param($P) Get-S1BinaryAbsolute $P} $Path }
function New-R4BDirectory($Path){ & $script:Legacy {param($P) New-S1BinaryDirectory $P} $Path }
function Write-R4BJson($Path,$Value){ & $script:Legacy {param($P,$V) Write-S1BinaryJson $P $V} $Path $Value }
function Write-R4BFile($Path,[byte[]]$Bytes){ & $script:Legacy {param($P,$B) Write-S1BinaryNewFile $P $B} $Path $Bytes }
function Get-R4BHash([byte[]]$Bytes){ & $script:Legacy {param($B) Get-S1BinaryHash $B} $Bytes }
function Read-R4BArchive($ZipPath,$Profile){ & $script:Legacy {param($P,$V) Read-S1BinaryZip $P $V} $ZipPath $Profile }
function Get-R4BImplementation($Root){
    return @(foreach($relative in @('packaging/S1BinaryReleasePackage.psm1','tools/package_s1_binary_release.ps1')){
        $path=Resolve-S1Path $Root $relative
        [ordered]@{path=$relative;size=[IO.FileInfo]::new($path).Length;sha256=(Get-R4BHash ([IO.File]::ReadAllBytes($path)))}
    })
}
function Read-S1ReleaseGenerationAuthorization($Root,[string]$AuthorizationPath){
    if(!$AuthorizationPath){$AuthorizationPath=Resolve-S1Path $Root $script:AuthorizationRelative}
    elseif(![IO.Path]::IsPathFullyQualified($AuthorizationPath)){$AuthorizationPath=Resolve-S1Path $Root $AuthorizationPath}
    Assert-S1Identity $AuthorizationPath 2170 $script:AuthorizationSha
    $record=Read-S1StrictJson $AuthorizationPath
    Assert-S1NoticeValue $record.approvalScope 'R4-B-PRIVATE-CANDIDATE-ONLY'
    Assert-S1NoticeValue $record.r4BGenerationAuthorized $true
    Assert-S1NoticeValue $record.designGenerationFlagMustRemain $false
    foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','githubReleaseAllowed','signingAllowed','legalApprovalGranted','sourceApprovalInherited','legacyPrivateFallbackAllowed')){
        Assert-S1NoticeValue $record[$key] $false
    }
    foreach($binding in @($record.releaseProfile,$record.releaseReader,$record.legacyHelpers)){
        Assert-S1Identity (Resolve-S1Path $Root $binding.path) $binding.size $binding.sha256
    }
    return $record
}
function Read-S1ReleasePackagingState($Root,$R2EvidenceRoot,$R3EvidenceRoot,[string]$AuthorizationPath,[switch]$ForCreation){
    $Root=Get-R4BAbsolute $Root
    $authorization=Read-S1ReleaseGenerationAuthorization $Root $AuthorizationPath
    $null=Read-S1BinaryReleasePlan $Root $R2EvidenceRoot $R3EvidenceRoot
    $profile=Read-S1BinaryReleaseProfile $Root
    Assert-S1NoticeValue $profile.r4BGenerationAuthorized $authorization.designGenerationFlagMustRemain
    Assert-S1NoticeValue $profile.sourceRevision $authorization.productSourceRevision
    Assert-S1NoticeValue $profile.openRights $authorization.inheritedOpenRights
    # R2 identity is the binary authority; a new Packaging HEAD is not a rebuild.
    $identity=Read-S1StrictJson (Resolve-S1Path $R2EvidenceRoot identity.json)
    $pe=@($profile.files|Where-Object kind -CEQ PE)
    Assert-S1InputSet @($pe.path) @($identity.identities.name)
    foreach($file in $pe){
        $accepted=@($identity.identities|Where-Object name -CEQ $file.path)[0]
        Assert-S1NoticeValue $file.size $accepted.size
        Assert-S1NoticeValue $file.sha256 $accepted.sha256
    }
    if($ForCreation){
        $head=& git --no-optional-locks -C $Root rev-parse HEAD
        if($LASTEXITCODE -ne 0 -or $head -cne $authorization.packagingHead){throw 'S1:R4B:PackagingHead'}
        $branch=& git --no-optional-locks -C $Root branch --show-current
        if($LASTEXITCODE -ne 0 -or $branch -cne $authorization.branch){throw 'S1:R4B:Branch'}
        $top=& git --no-optional-locks -C $Root rev-parse --show-toplevel
        if($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($top) -ine $Root){throw 'S1:R4B:GitRoot'}
    }
    return [pscustomobject]@{profile=$profile;authorization=$authorization;result='PRIVATE_GENERATION_AUTHORIZED';designGenerationFlag=$false;r4BGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Read-S1ReleasePackagingPlan($Root,$R2EvidenceRoot,$R3EvidenceRoot,[string]$AuthorizationPath){
    $state=Read-S1ReleasePackagingState $Root $R2EvidenceRoot $R3EvidenceRoot $AuthorizationPath -ForCreation
    return [pscustomobject]@{result=$state.result;files=45;PE=18;unchangedFromR3=43;designGenerationFlag=$false;r4BGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false;packagingRevision=$state.authorization.packagingHead;productSourceRevision=$state.profile.sourceRevision}
}
function Test-S1ReleaseCandidateTree($Root,$R2EvidenceRoot,$R3EvidenceRoot,$PackageRoot,[string]$AuthorizationPath){
    $state=Read-S1ReleasePackagingState $Root $R2EvidenceRoot $R3EvidenceRoot $AuthorizationPath
    $PackageRoot=Get-R4BAbsolute $PackageRoot
    Assert-S1NoticeTree $PackageRoot @($state.profile.files.path)
    foreach($file in $state.profile.files){Assert-S1Identity (Resolve-S1Path $PackageRoot $file.path) $file.size $file.sha256}
    return [pscustomobject]@{result='PASS';scope='PRIVATE_RELEASE_PAYLOAD_IDENTITY_ONLY';files=45;PEMatchesR2=18;unchangedFromR3=43;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Test-S1ReleaseCandidateZip($Root,$R2EvidenceRoot,$R3EvidenceRoot,$ZipPath,[string]$AuthorizationPath){
    $state=Read-S1ReleasePackagingState $Root $R2EvidenceRoot $R3EvidenceRoot $AuthorizationPath
    $zip=Read-R4BArchive $ZipPath $state.profile
    return [pscustomobject]@{result='PASS';scope='PRIVATE_RELEASE_ZIP_IDENTITY_ONLY';files=45;size=$zip.size;sha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Get-R4BOwnerRecord($State){
    return [ordered]@{
        state='OWNED_PRIVATE_OUTPUT_ONLY';reuseAllowed=$false;acceptanceRequiresSeparateReceipt=$true
        r4BGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false
        authorizationSha256=$script:AuthorizationSha;releaseProfileSha256=$script:DesignSha
        productSourceRevision=$State.profile.sourceRevision;packagingRevision=$State.authorization.packagingHead
    }
}
function Get-R4BAcceptanceRecord($Root,$State,$Zip){
    $profile=$State.profile
    $inventory=@($profile.files|ForEach-Object {[ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256}})
    $inventoryText=(($inventory|ForEach-Object {"{0}`t{1}`t{2}" -f $_.path,$_.size,$_.sha256}) -join "`n")+"`n"
    return [ordered]@{
        result='PASS';verdict='PASS WITH LIMITATIONS';purpose='R4-B-PRIVATE-RELEASE-CANDIDATE-ONLY'
        productSourceRevision=$profile.sourceRevision;packagingRevision=$State.authorization.packagingHead
        implementation=(Get-R4BImplementation $Root);authorizationId=$State.authorization.authorizationId
        authorizationSha256=$script:AuthorizationSha;releaseProfileSha256=$script:DesignSha
        designGenerationFlag=$false;r4BGenerationAuthorized=$true
        binaryReleaseApproved=$false;publicDistributionAllowed=$false;thirdPartyTransferAllowed=$false
        signed=$false;published=$false;PEMatchesR2=18;unchangedFromR3=43;files=45;thirdPartyOriginals=23
        zip=[ordered]@{path=$profile.zipName;size=$Zip.size;sha256=$Zip.sha256}
        inventoryEncoding='UTF-8; path TAB decimal-size TAB uppercase-SHA256 LF; final LF'
        inventorySha256=(Get-R4BHash ([Text.UTF8Encoding]::new($false).GetBytes($inventoryText)));inventory=$inventory
        openRights=$profile.openRights;MS002L='NEEDS CLARIFICATION';reuseAllowed=$false
        provenance='Unsigned local integrity measurements; not signed provenance or redistribution approval'
    }
}
function Test-S1ReleasePrivateCandidate($Root,$R2EvidenceRoot,$R3EvidenceRoot,$CandidateRoot,[string]$AuthorizationPath){
    $state=Read-S1ReleasePackagingState $Root $R2EvidenceRoot $R3EvidenceRoot $AuthorizationPath
    $CandidateRoot=Get-R4BAbsolute $CandidateRoot
    $expected=@('owner.json','acceptance.json',$state.profile.zipName)
    foreach($prefix in @('payload/','expanded/')){$expected+=@($state.profile.files|ForEach-Object {$prefix+$_.path})}
    Assert-S1NoticeTree $CandidateRoot $expected
    Assert-S1NoticeValue (Read-S1StrictJson (Resolve-S1Path $CandidateRoot owner.json)) (Get-R4BOwnerRecord $state) ReleaseOwnerRecord
    $null=Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot (Join-Path $CandidateRoot payload) $AuthorizationPath
    $null=Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot (Join-Path $CandidateRoot expanded) $AuthorizationPath
    $zip=Read-R4BArchive (Resolve-S1Path $CandidateRoot $state.profile.zipName) $state.profile
    Assert-S1NoticeValue (Read-S1StrictJson (Resolve-S1Path $CandidateRoot acceptance.json)) (Get-R4BAcceptanceRecord $Root $state $zip) ReleaseAcceptanceRecord
    return [pscustomobject]@{result='PASS';scope='PRIVATE_CANDIDATE_RECEIPT_AND_CONTENTS';files=45;PEMatchesR2=18;unchangedFromR3=43;size=$zip.size;sha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function New-S1ReleasePrivateCandidate($Root,$R2EvidenceRoot,$R3EvidenceRoot,$OutputRoot,[string]$AuthorizationPath){
    $Root=Get-R4BAbsolute $Root
    $OutputRoot=Get-R4BAbsolute $OutputRoot
    if(Test-Path -LiteralPath $OutputRoot){throw 'S1:R4B:ExistingOutput'}
    $state=Read-S1ReleasePackagingState $Root $R2EvidenceRoot $R3EvidenceRoot $AuthorizationPath -ForCreation
    $profile=$state.profile;$authorization=$state.authorization
    $relative=[IO.Path]::GetRelativePath($Root,$OutputRoot).Replace([char]92,[char]47)
    Assert-S1RelativePath $relative
    $container=$relative.Split('/')
    if($container.Count -lt 3 -or $container[0] -cne 'build' -or
        !@($authorization.outputPolicy.allowedContainerPrefixes|Where-Object {$relative.StartsWith($_,[StringComparison]::Ordinal)}).Count){throw 'S1:R4B:OutputScope'}
    # The isolated container must already exist; claim only a brand-new leaf.
    New-R4BDirectory $OutputRoot
    Write-R4BJson (Join-Path $OutputRoot owner.json) (Get-R4BOwnerRecord $state)
    $payload=Join-Path $OutputRoot payload
    New-R4BDirectory $payload
    foreach($file in $profile.files){
        $dest=Resolve-S1Path $payload $file.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        [IO.File]::Copy((Resolve-S1Path $Root $file.source),$dest,$false)
    }
    $null=Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot $payload $AuthorizationPath
    # Preserve the exact fixed timestamp text, independently of JSON DateKind or
    # the execution host's timezone. ZIP stores wall time without an offset.
    $document=[Text.Json.JsonDocument]::Parse([IO.File]::ReadAllText((Resolve-S1Path $Root $authorization.releaseProfile.path)))
    try{$entryDate=[DateTimeOffset]::Parse($document.RootElement.GetProperty('zipEntryDate').GetString(),[Globalization.CultureInfo]::InvariantCulture)}finally{$document.Dispose()}
    $zipPath=Join-Path $OutputRoot $profile.zipName
    $stream=[IO.File]::Open($zipPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try{
        foreach($file in $profile.files){
            $entry=$archive.CreateEntry($file.path,[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime=$entryDate;$entry.ExternalAttributes=32
            $output=$entry.Open();$input=[IO.File]::OpenRead((Resolve-S1Path $payload $file.path))
            try{$input.CopyTo($output)}finally{$input.Dispose();$output.Dispose()}
        }
    }finally{$archive.Dispose();$stream.Dispose()}
    # Unchanged R3 canonical ZIP validator: verify every entry before expansion.
    $zip=Read-R4BArchive $zipPath $profile
    $expanded=Join-Path $OutputRoot expanded
    New-R4BDirectory $expanded
    foreach($file in $profile.files){
        $dest=Resolve-S1Path $expanded $file.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        Write-R4BFile $dest $zip.contents[$file.path]
    }
    $null=Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot $expanded $AuthorizationPath
    $null=Read-S1ReleasePackagingState $Root $R2EvidenceRoot $R3EvidenceRoot $AuthorizationPath -ForCreation
    $receipt=Get-R4BAcceptanceRecord $Root $state $zip
    # No partial output, owner record or ZIP alone represents admitted output.
    Write-R4BJson (Join-Path $OutputRoot acceptance.json) $receipt
    return $receipt
}
Export-ModuleMember -Function Read-S1ReleaseGenerationAuthorization,Read-S1ReleasePackagingPlan,Test-S1ReleaseCandidateTree,Test-S1ReleaseCandidateZip,Test-S1ReleasePrivateCandidate,New-S1ReleasePrivateCandidate
