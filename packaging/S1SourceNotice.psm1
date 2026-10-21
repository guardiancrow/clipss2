#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Independent source-state reader. Imports only first-party strict contracts.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$script:S1CatalogSha='F0DD256170A8AC302046493A5F5D529D09A12C5EF24F749D1261091E193D75B5'
function Reject-S1Notice($Code){throw [IO.InvalidDataException]::new('S1:'+$Code)}
function Assert-S1NoticeObject($Value,$Keys){
    if($Value -isnot [Collections.IDictionary]){Reject-S1Notice ObjectType}
    if($Value.Count -ne $Keys.Count){Reject-S1Notice Fields}
    foreach($key in $Value.Keys){if($key -isnot [string] -or $key -cnotin $Keys){Reject-S1Notice Fields}}
}
function Assert-S1NoticeInteger($Value,$Expected){
    if(($Value -isnot [long] -and $Value -isnot [int]) -or $Value -ne $Expected){Reject-S1Notice IntegerValue}
}
function Assert-S1NoticeValue($Value,$Expected,$Code='ReviewedPolicy'){
    # Expected comes from code literals or the code-pinned reviewed catalog.
    if($null -eq $Expected){if($null -ne $Value){Reject-S1Notice $Code};return}
    if($Expected -is [string]){Assert-S1String $Value;if($Value -cne $Expected){Reject-S1Notice $Code};return}
    if($Expected -is [bool]){if($Value -isnot [bool] -or $Value -ne $Expected){Reject-S1Notice $Code};return}
    if($Expected -is [int] -or $Expected -is [long]){Assert-S1NoticeInteger $Value $Expected;return}
    if($Expected -is [Collections.IDictionary]){
        Assert-S1NoticeObject $Value @($Expected.Keys)
        foreach($key in $Expected.Keys){Assert-S1NoticeValue $Value[$key] $Expected[$key] $Code};return
    }
    if($Expected -is [array]){
        if($Value -isnot [array] -or $Value.Count -ne $Expected.Count){Reject-S1Notice ArrayShape}
        for($i=0;$i -lt $Expected.Count;$i++){Assert-S1NoticeValue $Value[$i] $Expected[$i] $Code};return
    }
    Reject-S1Notice UnsupportedPolicyType
}
function Assert-S1NoticePaths($Paths,$Count){
    if($Paths -isnot [array] -or $Paths.Count -ne $Count){Reject-S1Notice ArrayShape}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($path in $Paths){Assert-S1RelativePath $path;if(!$seen.Add($path)){Reject-S1Notice PathCollision}}
}
function Assert-S1NoticePublicPath($Path){
    Assert-S1RelativePath $Path
    if($Path -match '^(?i:\.git|build|dist|cache|\.codex|\.agents)(?:/|$)' -or
       $Path -match '^vcpkg-overlays/(?:libjxl|shipping-privacy)/' -or
       $Path -match '^packaging/(?:PortablePackage|ShippingSourceNotice)\.psm1$' -or
       $Path -match '^cmake/privacy/(?:managed-(?:sources|diagnostics)-v1|dependency-(?:inputs|diagnostics)-v1)\.' -or
       $Path -match '^packaging/public/(?:notice-anchor-v1|notice-anchor-shipping-v2|profiles-shipping-candidate-v2|source-notices-shipping-v2|profiles-s1-candidate-v1|notice-anchor-s1-v1|source-notices-s1-v1)\.json$' -or $Path -ceq 'THIRD-PARTY-NOTICES-S1-DRAFT.md'){Reject-S1Notice ScopeForbidden}
}
function Assert-S1NoticeOrigin($Origin){
    Assert-S1NoticeObject $Origin @('uri','revision');Assert-S1String $Origin.uri;Assert-S1String $Origin.revision
    $uri=$null
    if(![Uri]::TryCreate($Origin.uri,[UriKind]::Absolute,[ref]$uri) -or $uri.Scheme -cnotin @('https','urn') -or $uri.UserInfo){Reject-S1Notice OriginUri}
}
function Assert-S1NoticeIdentityRecord($Record){
    Assert-S1RelativePath $Record.path;Assert-S1String $Record.sha256
    if(($Record.size -isnot [int] -and $Record.size -isnot [long]) -or $Record.size -le 0 -or $Record.sha256 -cnotmatch '^[A-F0-9]{64}$'){Reject-S1Notice IdentityType}
}
function Get-S1SourceApproval($Catalog){
    return ,([ordered]@{status=$Catalog.status;sourceReleaseApproved=$Catalog.sourceReleaseApproved;binaryReleaseApproved=$Catalog.binaryReleaseApproved;openRights=$Catalog.openRights;resolvedRights=$Catalog.resolvedRights;approvalReferenceId=$Catalog.approvalReferenceId})
}
function Assert-S1SourceStatePolicy($Catalog){
    Assert-S1String $Catalog.status
    if($Catalog.status -cnotin @('source-development','source-release-approved')){Reject-S1Notice SourceState}
    Assert-S1NoticeValue $Catalog.binaryReleaseApproved $false BinaryApprovalForbidden
    Assert-S1NoticeValue $Catalog.binaryProfiles @() BinaryProfiles
    $rights=@('s1-included-source-rights-review','s1-final-source-identity','s1-publication-owner-approval')
    if($Catalog.status -ceq 'source-development'){
        Assert-S1NoticeValue $Catalog.sourceReleaseApproved $false Approval
        Assert-S1NoticeValue $Catalog.openRights $rights RightsResolution
        Assert-S1NoticeValue $Catalog.resolvedRights @() RightsResolution
        Assert-S1NoticeValue $Catalog.approvalReferenceId $null ApprovalReference
    }else{
        Assert-S1NoticeValue $Catalog.sourceReleaseApproved $true Approval
        Assert-S1NoticeValue $Catalog.openRights @() RightsResolution
        Assert-S1NoticeValue $Catalog.resolvedRights $rights RightsResolution
        if($Catalog.approvalReferenceId -isnot [string] -or $Catalog.approvalReferenceId -cnotmatch '^s1-source-approval-[0-9a-f]{32}$'){Reject-S1Notice ApprovalReference}
    }
}
function Assert-S1NoticeCatalog($Catalog){
    Assert-S1NoticeObject $Catalog @('schemaVersion','catalogId','sourceScopeId','status','sourceReleaseApproved','binaryReleaseApproved','binaryProfiles','approvalReferenceId','resolvedRights','anchor','directPaths','rawPaths','licensePaths','publicationPaths','componentPolicy','directPolicy','noticeSetPolicy','postimagePolicy','sourceInputBinding','openRights')
    Assert-S1NoticeValue $Catalog.schemaVersion 2
    Assert-S1NoticeValue $Catalog.catalogId 'clipss2-source-s1-release-v2' CatalogIdentity
    Assert-S1NoticeValue $Catalog.sourceScopeId 'clipss2-source-s1-v1' CatalogIdentity
    Assert-S1SourceStatePolicy $Catalog
    Assert-S1NoticeObject $Catalog.anchor @('id','path','size','sha256')
    Assert-S1NoticeValue $Catalog.anchor.id 'clipss2-source-s1-release-notices-v2' AnchorIdentity
    Assert-S1NoticeValue $Catalog.anchor.path 'packaging/public/notice-anchor-s1-source-release-v2.json' AnchorIdentity
    Assert-S1NoticeIdentityRecord $Catalog.anchor
    Assert-S1NoticePaths $Catalog.directPaths 15;Assert-S1NoticePaths $Catalog.rawPaths 43
    Assert-S1NoticePaths $Catalog.licensePaths 22;Assert-S1NoticePaths $Catalog.publicationPaths 288
    Assert-S1NoticePaths (@($Catalog.directPaths)+@($Catalog.rawPaths)) 58
    foreach($path in $Catalog.publicationPaths){Assert-S1NoticePublicPath $path}
    foreach($path in @($Catalog.directPaths)+@($Catalog.rawPaths)+@($Catalog.licensePaths)){if($path -cnotin $Catalog.publicationPaths){Reject-S1Notice ScopePolicy}}
    foreach($key in @('componentPolicy','directPolicy','noticeSetPolicy','postimagePolicy')){
        $count=@{componentPolicy=23;directPolicy=15;noticeSetPolicy=3;postimagePolicy=3}[$key]
        if($Catalog[$key] -isnot [array] -or $Catalog[$key].Count -ne $count){Reject-S1Notice ArrayShape}
    }
    $ids=@('source-s1-direct-v1','source-s1-license-notice-v1','source-s1-reference-v1')
    $purposes=@('source','documents','reference')
    for($i=0;$i -lt 3;$i++){
        $set=$Catalog.noticeSetPolicy[$i]
        Assert-S1NoticeObject $set @('id','purpose','status','profile','componentIds','rawFileIds','attributionIds')
        Assert-S1NoticeValue $set.id $ids[$i] NoticeSets
        Assert-S1NoticeValue $set.purpose $purposes[$i] NoticeSets
        $state=if($i -eq 2){'reference-only'}else{$Catalog.status}
        Assert-S1NoticeValue $set.status $state NoticeSets
    }
    Assert-S1NoticeValue $Catalog.sourceInputBinding ([ordered]@{inputId='clipss2-source-inputs-s1-v1';path='cmake/privacy/source-inputs-s1-v1.json';size=9645;sha256='80BD9B28EF236F29E31BC12B1D6C16120C9F7E60AC0ACF4F861691C6DF5C64AB';profile='v2';sourceScopeId='clipss2-source-s1-v1';overlayRoot='vcpkg-overlays/shipping-privacy-v2'}) SourceInputBinding
}
function Read-S1NoticeCatalog($Root){
    $path=Resolve-S1Path $Root 'packaging/public/profiles-s1-source-release-v2.json'
    if((Get-FileHash -LiteralPath $path).Hash -cne $script:S1CatalogSha){Reject-S1Notice CatalogHash}
    $catalog=Read-S1StrictJson $path;Assert-S1NoticeCatalog $catalog;return ,$catalog
}
function Assert-S1NoticeAnchor($Anchor,$Catalog){
    Assert-S1NoticeObject $Anchor @('schemaVersion','anchorId','sourceScopeId','originPublicCommit','approval','noticeSets','components','rawFiles','fileAttributions','sourceInputBinding','postimages')
    Assert-S1NoticeValue $Anchor.schemaVersion 2
    Assert-S1NoticeValue $Anchor.anchorId 'clipss2-source-s1-release-notices-v2' AnchorIdentity
    Assert-S1NoticeValue $Anchor.sourceScopeId 'clipss2-source-s1-v1' AnchorIdentity
    Assert-S1NoticeValue $Anchor.originPublicCommit 'd06bf89ce6a7430d97b7e896987e4e82c0dd2a51' AnchorIdentity
    Assert-S1NoticeValue $Anchor.approval (Get-S1SourceApproval $Catalog) Approval
    Assert-S1NoticeValue $Anchor.components $Catalog.componentPolicy ComponentPolicy
    Assert-S1NoticeValue $Anchor.fileAttributions $Catalog.directPolicy DirectPolicy
    Assert-S1NoticeValue $Anchor.noticeSets $Catalog.noticeSetPolicy NoticeSets
    Assert-S1NoticeValue $Anchor.postimages $Catalog.postimagePolicy Postimages
    Assert-S1NoticeValue $Anchor.sourceInputBinding $Catalog.sourceInputBinding SourceInputBinding
    if($Anchor.rawFiles -isnot [array] -or $Anchor.rawFiles.Count -ne 43){Reject-S1Notice ArrayShape}
    $rawMap=@{};$componentMap=@{};$attrMap=@{}
    for($i=0;$i -lt 43;$i++){
        $raw=$Anchor.rawFiles[$i]
        Assert-S1NoticeObject $raw @('id','path','size','sha256','sourceKind','origin','role')
        Assert-S1NoticeValue $raw.id ('raw-'+($i+1).ToString('00')) RawId
        Assert-S1NoticeValue $raw.path $Catalog.rawPaths[$i] RawPaths
        Assert-S1NoticeValue $raw.sourceKind 'repository' RawKind
        Assert-S1String $raw.role;if($raw.role -cnotin @('license','patent','notice','attribution','policy')){Reject-S1Notice RawKind}
        Assert-S1NoticeOrigin $raw.origin;Assert-S1NoticeIdentityRecord $raw
        $rawMap[$raw.id]=$raw
    }
    foreach($component in $Anchor.components){
        Assert-S1NoticeOrigin $component.origin
        if($componentMap.ContainsKey($component.id)){Reject-S1Notice ComponentPolicy}
        $componentMap[$component.id]=$component
        foreach($id in $component.rawFileIds){if(!$rawMap.ContainsKey($id)){Reject-S1Notice Reference}}
        if($component.distributionScope -ceq 'source-direct' -and ($component.selectionStatus -cne 'declared' -or $component.selectedLicense -isnot [string])){Reject-S1Notice ComponentPolicy}
    }
    if(@($Anchor.components|Where-Object distributionScope -CEQ source-direct).Count -ne 6 -or @($Anchor.components|Where-Object distributionScope -CEQ reference-only).Count -ne 17){Reject-S1Notice ComponentPolicy}
    for($i=0;$i -lt 15;$i++){
        $attr=$Anchor.fileAttributions[$i]
        Assert-S1NoticeValue $attr.path $Catalog.directPaths[$i] DirectPolicy
        Assert-S1NoticeIdentityRecord $attr
        Assert-S1NoticeValue $attr.profile 'shipping-v2' DirectPolicy
        Assert-S1NoticeValue $attr.effectiveV2Input $true DirectPolicy
        if($attr.gitBlob -isnot [string] -or $attr.gitBlob -cnotmatch '^[a-f0-9]{40}$' -or $attr.mode -isnot [string] -or $attr.mode -cnotmatch '^100(?:644|755)$'){Reject-S1Notice GitIdentity}
        foreach($origin in $attr.upstreamFiles){Assert-S1NoticeOrigin $origin}
        foreach($id in $attr.componentIds){if(!$componentMap.ContainsKey($id)){Reject-S1Notice Reference}}
        foreach($path in $attr.licenseRawPaths){if($path -cnotin $Catalog.rawPaths){Reject-S1Notice Reference}}
        $attrMap[$attr.id]=$attr
    }
    foreach($set in $Anchor.noticeSets){
        foreach($id in $set.componentIds){if(!$componentMap.ContainsKey($id)){Reject-S1Notice Reference}}
        foreach($id in $set.rawFileIds){if(!$rawMap.ContainsKey($id)){Reject-S1Notice Reference}}
        foreach($id in $set.attributionIds){if(!$attrMap.ContainsKey($id)){Reject-S1Notice Reference}}
    }
}
function Assert-S1NoticeInput($InputValue){
    Assert-S1NoticeValue $InputValue ([ordered]@{schemaVersion=2;purpose='source-release-notice';catalogId='clipss2-source-s1-release-v2';anchorId='clipss2-source-s1-release-notices-v2';noticeSetIds=@('source-s1-direct-v1','source-s1-license-notice-v1')}) NoticeInput
}
function Assert-S1NoticeTree($Root,$Expected,$Whole=$false){
    $files=Get-S1Files $Root
    try{Assert-S1InputSet $Expected @($files.Keys)}catch{if($_.Exception.Message -ceq 'S1:InputSet'){Reject-S1Notice ScopeFiles};throw}
    $dirs=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($path in $Expected){$slash=$path.LastIndexOf('/');while($slash -ge 0){$path=$path.Substring(0,$slash);[void]$dirs.Add($path);$slash=$path.LastIndexOf('/')}}
    $queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue($Root)
    while($queue.Count){foreach($entry in [IO.DirectoryInfo]::new($queue.Dequeue()).EnumerateFileSystemInfos()){
        if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){Reject-S1Notice ReparsePoint}
        if(($entry.Attributes -band [IO.FileAttributes]::Directory) -eq 0){continue}
        $relative=[IO.Path]::GetRelativePath($Root,$entry.FullName).Replace('\','/')
        if($Whole){Assert-S1NoticePublicPath $relative}
        if(!$dirs.Contains($relative)){Reject-S1Notice ScopeDirectories};$queue.Enqueue($entry.FullName)
    }}
}
function Assert-S1NoticeSourceBinding($Root,$Anchor){
    $binding=$Anchor.sourceInputBinding
    $ledger=Read-S1StrictJson (Resolve-S1Path $Root $binding.path)
    Assert-S1NoticeObject $ledger @('schemaVersion','inputId','sourceScopeId','profile','shippingPrivacy','builtinBaseline','targetTriplet','hostTriplet','overlayRoot','packages','files','outputPolicy')
    Assert-S1NoticeValue $ledger.schemaVersion 1
    foreach($key in @('inputId','sourceScopeId','profile','overlayRoot')){Assert-S1NoticeValue $ledger[$key] $binding[$key] SourceInputBinding}
    Assert-S1NoticeValue $ledger.shippingPrivacy 'ON' SourceInputBinding
    if($ledger.files -isnot [array] -or $ledger.files.Count -ne 28 -or $ledger.packages -isnot [array] -or $ledger.packages.Count -ne 16){Reject-S1Notice SourceInputBinding}
    $all=@($Anchor.rawFiles)+@($Anchor.fileAttributions)
    foreach($file in $ledger.files){
        Assert-S1NoticeObject $file @('path','size','sha256');Assert-S1NoticeIdentityRecord $file
        $match=@($all|Where-Object path -CEQ $file.path)
        if($match.Count -ne 1 -or $file.size -ne $match[0].size -or $file.sha256 -cne $match[0].sha256){Reject-S1Notice SourceInputBinding}
    }
    $null=Read-S1OriginContract $Root
    Assert-S1Identity (Resolve-S1Path $Root 'cmake/privacy/managed-diagnostics-v2.json') 2859 '4C83CDA89C029EFFF6EF88E3D33C2FEED57EB94B60882C4C1215CCD1BDA70E29'
    Assert-S1Identity (Resolve-S1Path $Root 'cmake/privacy/managed-sources-v2.tsv') 327881 '3D372FE265E962D845B8E6D375707071A30662A112B0F531928A006EDDF7F7F2'
}
function Read-S1SourceState($Root,[switch]$PublicationView){
    $catalog=Read-S1NoticeCatalog $Root
    $anchorPath=Resolve-S1Path $Root $catalog.anchor.path
    try{Assert-S1Identity $anchorPath $catalog.anchor.size $catalog.anchor.sha256}catch{if($_.Exception.Message -ceq 'S1:RawIdentity'){Reject-S1Notice AnchorHash};throw}
    $anchor=Read-S1StrictJson $anchorPath
    Assert-S1NoticeAnchor $anchor $catalog
    Assert-S1NoticeInput (Read-S1StrictJson (Resolve-S1Path $Root 'packaging/public/source-notices-s1-source-release-v2.json'))
    foreach($file in @($anchor.rawFiles)+@($anchor.fileAttributions)){Assert-S1Identity (Resolve-S1Path $Root $file.path) $file.size $file.sha256}
    foreach($file in $anchor.fileAttributions){
        if((Get-S1BlobOid ([IO.File]::ReadAllBytes((Resolve-S1Path $Root $file.path)))) -cne $file.gitBlob){Reject-S1Notice GitIdentity}
    }
    $overlay=Resolve-S1Path $Root 'vcpkg-overlays/shipping-privacy-v2'
    Assert-S1NoticeTree $overlay @($catalog.directPaths|ForEach-Object {$_.Substring('vcpkg-overlays/shipping-privacy-v2/'.Length)})
    $licenses=Resolve-S1Path $Root licenses
    Assert-S1NoticeTree $licenses (@($catalog.licensePaths|ForEach-Object {$_.Substring(9)})+@('SOURCES.md','.gitattributes'))
    Assert-S1NoticeSourceBinding $Root $anchor
    if($PublicationView){Assert-S1NoticeTree $Root $catalog.publicationPaths $true}
    return [pscustomobject]@{result='PASS';validationScope='NOTICE_AND_DECLARED_STATE_ONLY';schemaVersion=2;status=$catalog.status;candidateOnly=$false;declaredSourceReleaseApproved=$catalog.sourceReleaseApproved;binaryReleaseApproved=$false;binaryProfiles=0;openRights=$catalog.openRights;resolvedRights=$catalog.resolvedRights;approvalReferenceId=$catalog.approvalReferenceId;approvalScope='EXTERNALLY_ATTESTED_SNAPSHOT_ONLY';currentCheckoutOwnerApproval='NOT_VERIFIED';ownerApprovalEvidenceVerified=$false;publicationPermissionVerified=$false;direct=15;raw=43;licenses=22;components=23;sourceDirectComponents=6;referenceOnlyComponents=17;sourceInputs=28;publicationScopeChecked=[bool]$PublicationView;publicationPaths=288;wholeScopeBytesAuthenticated=$false;gitModePolicy='100644 planned; actual new Git tree/modes independently verified at authorized J6E';productBuild=$false;binaryAbiRuntimeVerified=$false}
}
function Read-S1SourceRelease($Root,[switch]$PublicationView){
    $result=Read-S1SourceState $Root -PublicationView:$PublicationView
    if($result.status -cne 'source-release-approved'){Reject-S1Notice SourceReleaseRequired}
    return $result
}
function Read-S1NoticeCandidate($Root,[switch]$PublicationView){Reject-S1Notice CandidateContractUnsupported}
Export-ModuleMember -Function Read-S1SourceState,Read-S1SourceRelease,Read-S1NoticeCandidate,Read-S1NoticeCatalog,Assert-S1NoticeCatalog,Assert-S1NoticeAnchor,Assert-S1NoticeInput,Assert-S1NoticeValue,Assert-S1NoticeTree
