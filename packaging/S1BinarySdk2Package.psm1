#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Phase 3-C SDK-2 generation. No prior profile, reader, writer or authorization is replaced.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
Import-Module (Join-Path $PSScriptRoot S1SourceNotice.psm1)
$script:ProfileSha='F08473A89016D76DF42BC236FC9D2D415B724714D674196B67F1935E27E7013D'
$script:AuthSha='03D3CC0B7E21BDC66CE581428CD9F0EA0C1BEE6006AF51A64F93E4090C3B10A6'
$old=Join-Path $PSScriptRoot S1BinaryFinalPackage.psm1
Assert-S1Identity $old 15276 '612D8E1818876ED5CC48A515ED9F5D294D4D9F9FFF21FE45D48C30E6733F0497'
$script:Legacy=Import-Module $old -PassThru
# Reuse the pinned R4-D path and CreateNew primitives, not its 45-entry ZIP parser.
function Get-Sdk2Absolute($Path){& $script:Legacy {param($P) Get-FinalAbsolute $P} $Path}
function New-Sdk2Directory($Path){& $script:Legacy {param($P) New-FinalDirectory $P} $Path}
function Write-Sdk2Json($Path,$Value){& $script:Legacy {param($P,$V) Write-FinalJson $P $V} $Path $Value}
function Write-Sdk2File($Path,[byte[]]$Bytes){& $script:Legacy {param($P,$B) Write-FinalFile $P $B} $Path $Bytes}
function Get-Sdk2Hash([byte[]]$Bytes){& $script:Legacy {param($B) Get-FinalHash $B} $Bytes}


function Reject-Sdk2Archive($Reason){throw ('S1:Sdk2:Zip:'+ $Reason)}
function Assert-Sdk2Tree($Path,$Profile){
    Assert-S1NoticeTree $Path @($Profile.files.path)
    foreach($file in $Profile.files){Assert-S1Identity (Resolve-S1Path $Path $file.path) $file.size $file.sha256}
}
function Assert-Sdk2ZipLayout([byte[]]$Bytes, $Profile) {
    # Only the canonical ZIP layout written below is accepted: no preamble,
    # extras, comments, encryption, descriptors, links, padding or trailing data.
    if ($Bytes.Length -lt 22) { Reject-Sdk2Archive ZipLayout }
    $end = $Bytes.Length - 22
    if ([BitConverter]::ToUInt32($Bytes,$end) -ne 0x06054b50 -or
        [BitConverter]::ToUInt16($Bytes,$end+4) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$end+6) -ne 0 -or
        [BitConverter]::ToUInt16($Bytes,$end+8) -ne 46 -or [BitConverter]::ToUInt16($Bytes,$end+10) -ne 46 -or
        [BitConverter]::ToUInt16($Bytes,$end+20) -ne 0) { Reject-Sdk2Archive ZipLayout }
    [long]$central = [BitConverter]::ToUInt32($Bytes,$end+16)
    [long]$centralSize = [BitConverter]::ToUInt32($Bytes,$end+12)
    if ($central + $centralSize -ne $end) { Reject-Sdk2Archive ZipLayout }
    [long]$cursor = $central
    [long]$local = 0
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $utf8 = [Text.UTF8Encoding]::new($false,$true)
    for ($i=0; $i -lt 46; $i++) {
        if ($cursor + 46 -gt $end -or [BitConverter]::ToUInt32($Bytes,$cursor) -ne 0x02014b50) { Reject-Sdk2Archive ZipLayout }
        $nameLength = [BitConverter]::ToUInt16($Bytes,$cursor+28)
        $flags = [BitConverter]::ToUInt16($Bytes,$cursor+8)
        $method = [BitConverter]::ToUInt16($Bytes,$cursor+10)
        [long]$compressed = [BitConverter]::ToUInt32($Bytes,$cursor+20)
        if ($cursor+46+$nameLength -gt $end -or $nameLength -eq 0 -or $flags -notin @(0,2048) -or $method -notin @(0,8) -or
            [BitConverter]::ToUInt16($Bytes,$cursor+30) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$cursor+32) -ne 0 -or
            [BitConverter]::ToUInt16($Bytes,$cursor+34) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$cursor+36) -ne 0 -or
            [BitConverter]::ToUInt32($Bytes,$cursor+38) -ne 32 -or
            [BitConverter]::ToUInt32($Bytes,$cursor+42) -ne $local) { Reject-Sdk2Archive ZipMetadata }
        $name = $utf8.GetString($Bytes,$cursor+46,$nameLength)
        Assert-S1RelativePath $name
        if (!$names.Add($name) -or $name -cne $Profile.files[$i].path) { Reject-Sdk2Archive ZipFiles }
        if ($local+30+$nameLength+$compressed -gt $central -or [BitConverter]::ToUInt32($Bytes,$local) -ne 0x04034b50 -or
            [BitConverter]::ToUInt16($Bytes,$local+6) -ne $flags -or [BitConverter]::ToUInt16($Bytes,$local+8) -ne $method -or
            [BitConverter]::ToUInt16($Bytes,$local+26) -ne $nameLength -or [BitConverter]::ToUInt16($Bytes,$local+28) -ne 0 -or
            $utf8.GetString($Bytes,$local+30,$nameLength) -cne $name) { Reject-Sdk2Archive ZipLayout }
        # Time/date, CRC and sizes must agree in both records.
        for ($field=0; $field -lt 16; $field++) {
            if ($Bytes[$local+10+$field] -ne $Bytes[$cursor+12+$field]) { Reject-Sdk2Archive ZipLayout }
        }
        if ([BitConverter]::ToUInt32($Bytes,$cursor+24) -ne $Profile.files[$i].size) { Reject-Sdk2Archive ZipSize }
        $local += 30 + $nameLength + $compressed
        $cursor += 46 + $nameLength
    }
    if ($cursor -ne $end -or $local -ne $central) { Reject-Sdk2Archive ZipLayout }
}
function Read-Sdk2Archive($ZipPath, $Profile) {
    $ZipPath = Get-Sdk2Absolute $ZipPath
    $max = [long](($Profile.files | Measure-Object size -Sum).Sum) + 1048576
    if (![IO.File]::Exists($ZipPath) -or [IO.FileInfo]::new($ZipPath).Length -gt $max) { Reject-Sdk2Archive ZipSize }
    $bytes = [IO.File]::ReadAllBytes($ZipPath)
    Assert-Sdk2ZipLayout $bytes $Profile
    $memory = [IO.MemoryStream]::new($bytes,$false)
    $archive = [IO.Compression.ZipArchive]::new($memory,[IO.Compression.ZipArchiveMode]::Read,$false)
    $contents = [Collections.Generic.Dictionary[string,byte[]]]::new([StringComparer]::Ordinal)
    try {
        if ($archive.Entries.Count -ne 46) { Reject-Sdk2Archive ZipFiles }
        for ($i=0; $i -lt 46; $i++) {
            $entry = $archive.Entries[$i]; $expected = $Profile.files[$i]
            if ($entry.FullName -cne $expected.path -or $entry.Length -ne $expected.size) { Reject-Sdk2Archive ZipFiles }
            $input = $entry.Open()
            try {
                $data = [byte[]]::new([int]$expected.size)
                $offset = 0
                while ($offset -lt $data.Length) {
                    $read = $input.Read($data,$offset,$data.Length-$offset)
                    if (!$read) { Reject-Sdk2Archive ZipSize }; $offset += $read
                }
                if ($input.ReadByte() -ne -1 -or (Get-Sdk2Hash $data) -cne $expected.sha256) { Reject-Sdk2Archive ZipHash }
                $contents.Add($entry.FullName,$data)
            } finally { $input.Dispose() }
        }
    } finally { $archive.Dispose(); $memory.Dispose() }
    return [pscustomobject]@{ contents=$contents; size=$bytes.Length; sha256=(Get-Sdk2Hash $bytes) }
}
function Read-S1Sdk2Authorization($Root,[string]$AuthorizationPath){
    if(!$AuthorizationPath){$AuthorizationPath=Resolve-S1Path $Root 'packaging/public/authorization-s1-sdk2-private-v1.json'}
    elseif(![IO.Path]::IsPathFullyQualified($AuthorizationPath)){$AuthorizationPath=Resolve-S1Path $Root $AuthorizationPath}
    Assert-S1Identity $AuthorizationPath 1658 $script:AuthSha
    $auth=Read-S1StrictJson $AuthorizationPath
    Assert-S1NoticeValue $auth.approvalScope 'PHASE3C-SDK2-PRIVATE-CANDIDATE-ONLY'
    Assert-S1NoticeValue $auth.candidateGenerationAuthorized $true
    foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','githubReleaseAllowed','signingAllowed','legalApprovalGranted','reuseAllowed','legacyFallbackAllowed')){Assert-S1NoticeValue $auth[$key] $false}
    foreach($binding in @($auth.releaseProfile,$auth.legacyImplementation,$auth.legacyCLI)){
        Assert-S1Identity (Resolve-S1Path $Root $binding.path) $binding.size $binding.sha256
    }
    return $auth
}
function Read-S1Sdk2State($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,[string]$AuthorizationPath,[switch]$ForCreation){
    $Root=Get-Sdk2Absolute $Root
    $auth=Read-S1Sdk2Authorization $Root $AuthorizationPath
    $profilePath=Resolve-S1Path $Root $auth.releaseProfile.path
    if((Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash -cne $script:ProfileSha){throw 'S1:RawIdentity:Sdk2Profile'}
    $p=Read-S1StrictJson $profilePath
    foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','thirdPartyTransferAllowed','legalApprovalGranted','sourceApprovalInherited','privateApprovalInherited')){Assert-S1NoticeValue $p[$key] $false}
    Assert-S1NoticeValue $p.profileId 'clipss2-s1-binary-release-v3-sdk2'
    Assert-S1NoticeValue $p.microsoftStatus.skillVersion '1.3.0'
    Assert-S1NoticeValue $p.microsoftStatus.MSVC1 'INTERPRETED-APPROVED'
    Assert-S1NoticeValue $p.microsoftStatus.TOOLS1 'CLEAR'
    Assert-S1NoticeValue $p.microsoftStatus.SDK2 'CLEAR IF'
    Assert-S1NoticeValue $p.microsoftStatus.G3 'NOT AUTHORIZED'
    Assert-S1NoticeValue $p.microsoftStatus.ownerWordingSelected $true
    Assert-S1NoticeValue $p.microsoftStatus.equivalentProtectionVerified $false
    Assert-S1NoticeValue $p.microsoftStatus.recipientAgreementEstablished $false
    Assert-S1NoticeValue $p.microsoftStatus.downstreamFulfillmentVerified $false
    Assert-S1NoticeValue $p.microsoftStatus.ownerResidualRiskAccepted $false
    Assert-S1NoticeValue $p.sourceRevision $auth.productSourceRevision
    Assert-S1NoticeValue $p.packagingBaseRevision $auth.packagingHead
    # The old validator retains its own raw hashes and historical legal states.
    # Read-only validation accepts a later HEAD; the old creation gate is untouched.
    foreach($binding in $p.r4dBindings){Assert-S1Identity (Resolve-S1Path $R4DEvidenceRoot $binding.path) $binding.size $binding.sha256}
    $null=& $script:Legacy {param($R,$E2,$E3,$EB,$ED) Test-S1FinalCandidate $R $E2 $E3 $EB (Resolve-S1Path $ED 'candidate')} $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot
    Assert-S1Identity (Resolve-S1Path $Root $p.parentProfile.path) $p.parentProfile.size $p.parentProfile.sha256
    $parent=Read-S1StrictJson (Resolve-S1Path $Root $p.parentProfile.path)
    foreach($key in @('sourceRevision','branch','preset','sourceScopeId','sourceInputs','sourceNoticeBindings','r2Bindings','r3Bindings','runtime','stlLicenseOrigin','components','PECount','licenseCount')){Assert-S1NoticeValue $p[$key] $parent[$key] Sdk2Inheritance}
    if($p.files.Count -ne 46 -or $parent.files.Count -ne 45){throw 'S1:Sdk2:FileCount'}
    Assert-S1InputSet (@($parent.files.path)+@('MICROSOFT-COMPONENT-TERMS.txt')) @($p.files.path)
    foreach($file in $p.files){
        if($file.path -ceq 'MICROSOFT-COMPONENT-TERMS.txt'){
            Assert-S1NoticeValue $file.source 'packaging/binary/MICROSOFT-COMPONENT-TERMS.txt'
            Assert-S1NoticeValue $file.kind 'component-terms'
            Assert-S1Identity (Resolve-S1Path $Root $file.source) $file.size $file.sha256
            continue
        }
        $prior=@($parent.files|Where-Object path -CEQ $file.path)[0]
        if($file.path -ceq 'README.md'){Assert-S1NoticeValue $file.source 'packaging/binary/README-portable-s1-release-v3.md';Assert-S1NoticeValue $file.kind $prior.kind}
        elseif($file.path -ceq 'THIRD-PARTY-NOTICES.md'){Assert-S1NoticeValue $file.source 'packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v3.md';Assert-S1NoticeValue $file.kind $prior.kind}
        else{Assert-S1NoticeValue $file $prior Sdk2PayloadInheritance}
        Assert-S1Identity (Resolve-S1Path $Root $file.source) $file.size $file.sha256
    }
    Assert-S1Identity (Resolve-S1Path $Root $p.decisionRecord.path) $p.decisionRecord.size $p.decisionRecord.sha256
    $priorReceipt=Read-S1StrictJson (Resolve-S1Path $R4DEvidenceRoot 'candidate/acceptance.json')
    $expected=@($parent.files|ForEach-Object {[ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256}})
    Assert-S1NoticeValue $priorReceipt.inventory $expected Sdk2ParentInventory
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
            if($target -cnotin $p.files.path){throw 'S1:Sdk2:DocumentReference'}
        }
    }
    if($ForCreation){
        $head=& git --no-optional-locks -C $Root rev-parse HEAD
        if($LASTEXITCODE -ne 0 -or $head -cne $auth.packagingHead){throw 'S1:Sdk2:PackagingHead'}
        $branch=& git --no-optional-locks -C $Root branch --show-current
        if($LASTEXITCODE -ne 0 -or $branch -cne $auth.branch){throw 'S1:Sdk2:Branch'}
        $top=& git --no-optional-locks -C $Root rev-parse --show-toplevel
        if($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($top) -ine $Root){throw 'S1:Sdk2:GitRoot'}
    }
    return [pscustomobject]@{profile=$p;authorization=$auth;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Read-S1Sdk2Plan($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,[string]$AuthorizationPath){
    $state=Read-S1Sdk2State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $AuthorizationPath -ForCreation
    return [pscustomobject]@{result='PRIVATE_SDK2_GENERATION_AUTHORIZED';files=46;PEMatchesR2=18;unchangedFromR4D=43;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false;productSourceRevision=$state.profile.sourceRevision;packagingRevision=$state.authorization.packagingHead;zipName=$state.profile.zipName}
}
function Test-S1Sdk2Tree($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$PackageRoot,[string]$AuthorizationPath){
    $state=Read-S1Sdk2State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $AuthorizationPath
    $PackageRoot=Get-Sdk2Absolute $PackageRoot
    Assert-Sdk2Tree $PackageRoot $state.profile
    return [pscustomobject]@{result='PASS';files=46;PEMatchesR2=18;unchangedFromR4D=43;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Test-S1Sdk2Zip($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$ZipPath,[string]$AuthorizationPath){
    $state=Read-S1Sdk2State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $AuthorizationPath
    $zip=Read-Sdk2Archive $ZipPath $state.profile
    return [pscustomobject]@{result='PASS';files=46;size=$zip.size;sha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function Get-Sdk2Owner($State){return [ordered]@{state='OWNED_PRIVATE_SDK2_CANDIDATE';reuseAllowed=$false;acceptanceRequiresSeparateReceipt=$true;candidateGenerationAuthorized=$true;binaryReleaseApproved=$false;publicDistributionAllowed=$false;authorizationSha256=$script:AuthSha;profileSha256=$script:ProfileSha;productSourceRevision=$State.profile.sourceRevision;packagingRevision=$State.authorization.packagingHead}}
function Get-Sdk2Receipt($Root,$State,$Zip){
    $inventory=@($State.profile.files|ForEach-Object {[ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256}})
    $text=(($inventory|ForEach-Object {"{0}`t{1}`t{2}" -f $_.path,$_.size,$_.sha256}) -join "`n")+"`n"
    $implementation=@(foreach($relative in @('packaging/S1BinarySdk2Package.psm1','tools/package_s1_binary_sdk2.ps1')){$path=Resolve-S1Path $Root $relative;[ordered]@{path=$relative;size=[IO.FileInfo]::new($path).Length;sha256=(Get-Sdk2Hash ([IO.File]::ReadAllBytes($path)))}})
    return [ordered]@{
        result='PASS';verdict='PASS WITH LIMITATIONS';purpose='PHASE3C-SDK2-PRIVATE-CANDIDATE-ONLY'
        productSourceRevision=$State.profile.sourceRevision;packagingRevision=$State.authorization.packagingHead;implementation=$implementation
        authorizationSha256=$script:AuthSha;profileSha256=$script:ProfileSha;candidateGenerationAuthorized=$true
        binaryReleaseApproved=$false;publicDistributionAllowed=$false;thirdPartyTransferAllowed=$false;legalApprovalGranted=$false
        signed=$false;published=$false;tagCreated=$false;files=46;PEMatchesR2=18;unchangedFromR4D=43;thirdPartyOriginals=23
        releaseProposal=$State.profile.releaseProposal
        zip=[ordered]@{path=$State.profile.zipName;size=$Zip.size;sha256=$Zip.sha256}
        inventoryEncoding='UTF-8; path TAB decimal-size TAB uppercase-SHA256 LF; final LF';inventorySha256=(Get-Sdk2Hash ([Text.UTF8Encoding]::new($false).GetBytes($text)));inventory=$inventory
        G1='VERIFIED';MSVC1='INTERPRETED-APPROVED';TOOLS1='CLEAR';SDK1='CONDITIONAL';SDK2='CLEAR IF';G3='NOT AUTHORIZED'
        ownerWordingSelected=$true;equivalentProtectionVerified=$false;recipientAgreementEstablished=$false
        packageTermsPresent=$true;releasePagePublished=$false;downstreamFulfillmentVerified=$false;ownerResidualRiskAccepted=$false;reuseAllowed=$false
        provenance='Unsigned local integrity measurements; not license grant or publication approval'
    }
}
function Test-S1Sdk2Candidate($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$CandidateRoot,[string]$AuthorizationPath){
    $state=Read-S1Sdk2State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $AuthorizationPath
    $CandidateRoot=Get-Sdk2Absolute $CandidateRoot
    $paths=@('owner.json','acceptance.json',$state.profile.zipName)
    foreach($prefix in @('payload/','expanded/')){$paths+=@($state.profile.files|ForEach-Object {$prefix+$_.path})}
    Assert-S1NoticeTree $CandidateRoot $paths
    Assert-S1NoticeValue (Read-S1StrictJson (Resolve-S1Path $CandidateRoot owner.json)) (Get-Sdk2Owner $state) Sdk2Owner
    foreach($dir in @('payload','expanded')){Assert-Sdk2Tree (Join-Path $CandidateRoot $dir) $state.profile}
    $zip=Read-Sdk2Archive (Resolve-S1Path $CandidateRoot $state.profile.zipName) $state.profile
    Assert-S1NoticeValue (Read-S1StrictJson (Resolve-S1Path $CandidateRoot acceptance.json)) (Get-Sdk2Receipt $Root $state $zip) Sdk2Receipt
    return [pscustomobject]@{result='PASS';scope='PRIVATE_SDK2_CANDIDATE_ONLY';files=46;PEMatchesR2=18;unchangedFromR4D=43;size=$zip.size;sha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false}
}
function New-S1Sdk2Candidate($Root,$R2EvidenceRoot,$R3EvidenceRoot,$R4BEvidenceRoot,$R4DEvidenceRoot,$OutputRoot,[string]$AuthorizationPath){
    $Root=Get-Sdk2Absolute $Root;$OutputRoot=Get-Sdk2Absolute $OutputRoot
    if(Test-Path -LiteralPath $OutputRoot){throw 'S1:Sdk2:ExistingOutput'}
    $state=Read-S1Sdk2State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $AuthorizationPath -ForCreation
    $p=$state.profile
    $relative=[IO.Path]::GetRelativePath($Root,$OutputRoot).Replace([char]92,[char]47)
    Assert-S1RelativePath $relative
    if($relative.Split('/').Count -lt 3 -or !@($state.authorization.outputPrefixes|Where-Object {$relative.StartsWith($_,[StringComparison]::Ordinal)}).Count){throw 'S1:Sdk2:OutputScope'}
    New-Sdk2Directory $OutputRoot
    Write-Sdk2Json (Join-Path $OutputRoot owner.json) (Get-Sdk2Owner $state)
    $payload=Join-Path $OutputRoot payload;New-Sdk2Directory $payload
    foreach($file in $p.files){
        $dest=Resolve-S1Path $payload $file.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        $source=if($file.path -cin @('README.md','THIRD-PARTY-NOTICES.md','MICROSOFT-COMPONENT-TERMS.txt')){Resolve-S1Path $Root $file.source}else{Resolve-S1Path $R4DEvidenceRoot ('candidate/expanded/'+$file.path)}
        [IO.File]::Copy($source,$dest,$false)
    }
    Assert-Sdk2Tree $payload $p
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
    $zip=Read-Sdk2Archive $zipPath $p
    $expanded=Join-Path $OutputRoot expanded;New-Sdk2Directory $expanded
    foreach($file in $p.files){$dest=Resolve-S1Path $expanded $file.path;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest));Write-Sdk2File $dest $zip.contents[$file.path]}
    Assert-Sdk2Tree $expanded $p
    $null=Read-S1Sdk2State $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $AuthorizationPath -ForCreation
    $receipt=Get-Sdk2Receipt $Root $state $zip
    Write-Sdk2Json (Join-Path $OutputRoot acceptance.json) $receipt
    return $receipt
}
Export-ModuleMember -Function Read-S1Sdk2Authorization,Read-S1Sdk2Plan,Test-S1Sdk2Tree,Test-S1Sdk2Zip,Test-S1Sdk2Candidate,New-S1Sdk2Candidate
