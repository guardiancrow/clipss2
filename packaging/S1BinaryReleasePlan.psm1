#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# R4-A design reader only: no package writer, approval mutation or Git operation.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
Import-Module (Join-Path $PSScriptRoot S1SourceNotice.psm1)
Import-Module (Join-Path $PSScriptRoot S1BinaryPackage.psm1)
$script:ReleasePath = 'packaging/public/profile-s1-binary-release-v1.json'
$script:ReleaseSha = 'CD7F936F638FA634C8E335E93DB7D3566661B7F6E72C842206B620F9ECE4D29B'

function Read-S1BinaryReleaseProfile($Root) {
    $path = Resolve-S1Path $Root $script:ReleasePath
    Assert-S1Identity $path 29156 $script:ReleaseSha
    $profile = Read-S1StrictJson $path
    Assert-S1NoticeValue $profile.schemaVersion 2
    Assert-S1NoticeValue $profile.status 'release-design-unapproved'
    foreach ($key in @('r4BGenerationAuthorized','binaryReleaseApproved','publicDistributionAllowed','sourceApprovalInherited','privateApprovalInherited','zipGenerated')) {
        Assert-S1NoticeValue $profile[$key] $false
    }
    foreach ($key in @('finalZipSha256','finalReleaseVersion','finalTag','finalTargetCommit','finalRepository')) {
        Assert-S1NoticeValue $profile[$key] $null
    }
    Assert-S1NoticeValue $profile.distributionPolicy.approvalScope 'DISTRIBUTION_METHOD_ONLY'
    Assert-S1NoticeValue $profile.microsoftStatus.MS002L 'NEEDS CLARIFICATION'
    Assert-S1NoticeValue $profile.microsoftStatus.downstreamConditions 'NEEDS CLARIFICATION'
    Assert-S1NoticeValue $profile.microsoftStatus.microsoftInquiryMandatory $false
    Assert-S1NoticeValue $profile.microsoftStatus.ownerRiskAcceptanceWaivesConditions $false
    Assert-S1NoticeValue $profile.readerBoundary.packageWriterSupported $false
    Assert-S1NoticeValue $profile.otherPC.independentlyVerified $false
    Assert-S1NoticeValue $profile.otherPC.exhaustiveFeatureAcceptance $false
    foreach ($decision in $profile.ownerDecisions) { Assert-S1NoticeValue $decision.status 'PENDING' }
    return $profile
}

function Read-S1BinaryReleasePlan($Root, $R2EvidenceRoot, $R3EvidenceRoot) {
    $profile = Read-S1BinaryReleaseProfile $Root
    # This legacy entry reads the code-pinned profile, not Read-S1BinaryPlan:
    # it never requires current HEAD to equal the historical product revision.
    $parent = Read-S1BinaryProfile $Root
    Assert-S1Identity (Resolve-S1Path $Root $profile.parentProfile.path) $profile.parentProfile.size $profile.parentProfile.sha256
    foreach ($key in @('sourceRevision','branch','preset','sourceScopeId','sourceInputs','sourceNoticeBindings','r2Bindings','runtime','stlLicenseOrigin','components','PECount','licenseCount')) {
        Assert-S1NoticeValue $profile[$key] $parent[$key] ReleaseInheritance
    }
    Assert-S1InputSet @($parent.files.path) @($profile.files.path)
    foreach ($file in $profile.files) {
        $old = @($parent.files | Where-Object path -CEQ $file.path)[0]
        if ($file.path -ceq 'README.md') {
            Assert-S1NoticeValue $file.source 'packaging/binary/README-portable-s1-release-v1.md'
            Assert-S1NoticeValue $file.kind $old.kind
        } elseif ($file.path -ceq 'THIRD-PARTY-NOTICES.md') {
            Assert-S1NoticeValue $file.source 'packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v1.md'
            Assert-S1NoticeValue $file.kind $old.kind
        } else {
            Assert-S1NoticeValue $file $old ReleasePayloadInheritance
        }
        Assert-S1Identity (Resolve-S1Path $Root $file.source) $file.size $file.sha256
    }
    Assert-S1Identity (Resolve-S1Path $Root $profile.policyDocument.path) $profile.policyDocument.size $profile.policyDocument.sha256
    # Existing fixed NOTICE/input identities are reused without PublicationView
    # or a fresh source/privacy/license audit. No source approval is inherited.
    $null = Read-S1SourceState $Root
    foreach ($file in @($profile.sourceInputs) + @($profile.sourceNoticeBindings)) {
        Assert-S1Identity (Resolve-S1Path $Root $file.path) $file.size $file.sha256
    }
    foreach ($file in $profile.r2Bindings) {
        Assert-S1Identity (Resolve-S1Path $R2EvidenceRoot $file.path) $file.size $file.sha256
    }
    foreach ($file in $profile.r3Bindings) {
        Assert-S1Identity (Resolve-S1Path $R3EvidenceRoot $file.path) $file.size $file.sha256
    }
    $r2 = Read-S1StrictJson (Resolve-S1Path $R2EvidenceRoot result.json)
    Assert-S1NoticeValue $r2.revision $profile.sourceRevision
    Assert-S1NoticeValue $r2.binaryReleaseApproved $false
    $r3 = Read-S1StrictJson (Resolve-S1Path $R3EvidenceRoot 'candidate/acceptance.json')
    Assert-S1NoticeValue $r3.sourceRevision $profile.sourceRevision
    Assert-S1NoticeValue $r3.binaryReleaseApproved $false
    Assert-S1NoticeValue $r3.publicDistributionAllowed $false
    Assert-S1NoticeValue $r3.inventorySha256 $profile.r3InventorySha256
    Assert-S1NoticeValue $r3.zip.sha256 $profile.otherPC.zipSha256
    $expectedInventory = @($parent.files | ForEach-Object { [ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256} })
    Assert-S1NoticeValue $r3.inventory $expectedInventory ReleaseParentInventory
    # Package-local documentation references must resolve in the 45-file plan.
    foreach ($file in @($profile.files | Where-Object { $_.path -cin @('README.md','THIRD-PARTY-NOTICES.md') })) {
        $text = [IO.File]::ReadAllText((Resolve-S1Path $Root $file.source))
        foreach ($link in [regex]::Matches($text,'\]\(([^)]+)\)')) {
            $target = $link.Groups[1].Value
            if ($target.StartsWith('https://',[StringComparison]::Ordinal)) { continue }
            Assert-S1RelativePath $target
            if ($target -cnotin $profile.files.path) { throw 'S1:Release:DocumentReference' }
        }
    }
    return [pscustomobject]@{
        result='DRAFT_VALIDATED';validationScope='R4-A-DOCUMENTS-AND-IDENTITY-BINDINGS-ONLY'
        productSourceRevision=$profile.sourceRevision;packagingBaseRevision=$profile.packagingBaseRevision
        files=45;PE=18;thirdPartyOriginals=23;payloadChanges=@('README.md','THIRD-PARTY-NOTICES.md')
        MS002T='VERIFIED';MS002L='NEEDS CLARIFICATION';MS002SDK='CONDITIONAL'
        otherPC='USER_REPORTED_PASS';r4BGenerationAuthorized=$false;packageWriterSupported=$false
        binaryReleaseApproved=$false;publicDistributionAllowed=$false;profileSha256=$script:ReleaseSha
    }
}
Export-ModuleMember -Function Read-S1BinaryReleaseProfile,Read-S1BinaryReleasePlan
