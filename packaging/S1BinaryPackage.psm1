#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Independent, unsigned local identity contract. Never grants distribution rights.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
Import-Module (Join-Path $PSScriptRoot S1SourceNotice.psm1)
$script:ProfilePath = 'packaging/public/profile-s1-binary-private-v1.json'
$script:ProfileSha = '3E5068E3686E792DB0989405C057FE6EC4EC78AD67F44D4CA70FC702FEC68152'

function Reject-S1Binary($Code) { throw [IO.InvalidDataException]::new('S1:Binary:' + $Code) }
function Get-S1BinaryHash([byte[]]$Bytes) { [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)) }
function Get-S1BinaryAbsolute($Path) {
    if (![IO.Path]::IsPathFullyQualified($Path) -or $Path -match '^[/\\]{2}') { Reject-S1Binary AbsolutePath }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd([char[]]@('/', '\'))
    Assert-S1NoReparse $full
    return $full
}
function Read-S1BinaryProfile($Root) {
    $path = Resolve-S1Path $Root $script:ProfilePath
    Assert-S1Identity $path 24871 $script:ProfileSha
    $profile = Read-S1StrictJson $path
    foreach ($key in @('binaryReleaseApproved','publicDistributionAllowed','sourceApprovalInherited')) {
        Assert-S1NoticeValue $profile[$key] $false
    }
    Assert-S1NoticeValue $profile.openRights @('MS-002','final-binary-owner-approval')
    if ($profile.files.Count -ne 45 -or $profile.PECount -ne 18 -or $profile.licenseCount -ne 23) { Reject-S1Binary ProfileCounts }
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $profile.files) {
        Assert-S1RelativePath $file.path
        Assert-S1RelativePath $file.source
        if (!$paths.Add($file.path)) { Reject-S1Binary ProfileCollision }
    }
    $pe = @($profile.files | Where-Object kind -CEQ PE | ForEach-Object path)
    $licenses = @($profile.files | Where-Object { $_.kind -cin @('product-license','license-or-patent') } | ForEach-Object path)
    $coverage = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($component in $profile.components) {
        foreach ($path in $component.licensePaths) { if ($path -cnotin $licenses) { Reject-S1Binary LicenseReference } }
        foreach ($path in $component.PE) {
            if ($path -cnotin $pe) { Reject-S1Binary PEReference }
            [void]$coverage.Add($path)
        }
    }
    Assert-S1InputSet $pe @($coverage)
    return $profile
}
function Read-S1BinaryPlan($Root, $EvidenceRoot) {
    $Root = Get-S1BinaryAbsolute $Root
    $EvidenceRoot = Get-S1BinaryAbsolute $EvidenceRoot
    $profile = Read-S1BinaryProfile $Root
    # Reuse the existing raw NOTICE/input checks, never its source approval.
    $null = Read-S1SourceState $Root
    foreach ($file in @($profile.sourceInputs) + @($profile.sourceNoticeBindings)) {
        Assert-S1Identity (Resolve-S1Path $Root $file.path) $file.size $file.sha256
    }
    foreach ($file in $profile.r2Bindings) {
        Assert-S1Identity (Resolve-S1Path $EvidenceRoot $file.path) $file.size $file.sha256
    }
    $identity = Read-S1StrictJson (Resolve-S1Path $EvidenceRoot identity.json)
    $result = Read-S1StrictJson (Resolve-S1Path $EvidenceRoot result.json)
    Assert-S1NoticeValue $identity.revision $profile.sourceRevision
    Assert-S1NoticeValue $identity.preset $profile.preset
    Assert-S1NoticeValue $identity.PECount 18
    Assert-S1NoticeValue $result.revision $profile.sourceRevision
    Assert-S1NoticeValue $result.verdict 'PASS WITH LIMITATIONS'
    Assert-S1NoticeValue $result.binaryReleaseApproved $false
    Assert-S1NoticeValue $result.productHashesUnchanged $true
    $pe = @($profile.files | Where-Object kind -CEQ PE)
    Assert-S1InputSet @($pe.path) @($identity.identities.name)
    foreach ($file in $profile.files) {
        Assert-S1Identity (Resolve-S1Path $Root $file.source) $file.size $file.sha256
        if ($file.kind -ceq 'PE') {
            $accepted = @($identity.identities | Where-Object name -CEQ $file.path)[0]
            Assert-S1NoticeValue $accepted.size $file.size
            Assert-S1NoticeValue $accepted.sha256 $file.sha256
        }
    }
    # Preserve the complete new STL license text while honoring existing LF
    # Git attributes. Reconstruct its upstream CRLF bytes, not a new paraphrase.
    $stlBytes = [IO.File]::ReadAllBytes((Resolve-S1Path $Root 'packaging/binary/msvc-stl-LICENSE.txt'))
    $stlText = [Text.UTF8Encoding]::new($false,$true).GetString($stlBytes)
    if ($stlText.Contains("`r")) { Reject-S1Binary LicenseNormalization }
    $upstream = [Text.UTF8Encoding]::new($false).GetBytes($stlText.Replace("`n","`r`n"))
    if ($upstream.Length -ne $profile.stlLicenseOrigin.upstreamSize -or
        (Get-S1BinaryHash $upstream) -cne $profile.stlLicenseOrigin.upstreamSha256) { Reject-S1Binary LicenseNormalization }
    # Product-directory immediate PE scope is exactly the R2 scope. Never glob-copy.
    $product = Resolve-S1Path $Root 'build/windows-x64-s1-v2'
    $actual = @(foreach ($file in [IO.DirectoryInfo]::new($product).EnumerateFiles()) {
        Assert-S1NoReparse $file.FullName
        $stream = $file.OpenRead()
        try { $mz = $stream.ReadByte() -eq 77 -and $stream.ReadByte() -eq 90 } finally { $stream.Dispose() }
        if ($mz -or $file.Extension -in @('.exe','.dll')) { $file.Name }
    })
    Assert-S1InputSet @($pe.path) $actual
    $revision = & git --no-optional-locks -C $Root rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $revision -cne $profile.sourceRevision) { Reject-S1Binary GitRevision }
    $branch = & git --no-optional-locks -C $Root branch --show-current
    if ($LASTEXITCODE -ne 0 -or $branch -cne $profile.branch) { Reject-S1Binary GitBranch }
    $top = & git --no-optional-locks -C $Root rev-parse --show-toplevel
    if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($top) -ine $Root) { Reject-S1Binary GitRoot }
    return [pscustomobject]@{ profile=$profile; result='PASS'; mode='PRIVATE_VALIDATION_ONLY'; binaryReleaseApproved=$false; publicDistributionAllowed=$false; PE=18; files=45; licenses=23; openRights=$profile.openRights }
}
function Test-S1BinaryTree($Root, $EvidenceRoot, $PackageRoot) {
    $plan = Read-S1BinaryPlan $Root $EvidenceRoot
    $PackageRoot = Get-S1BinaryAbsolute $PackageRoot
    Assert-S1NoticeTree $PackageRoot @($plan.profile.files.path)
    foreach ($file in $plan.profile.files) {
        Assert-S1Identity (Resolve-S1Path $PackageRoot $file.path) $file.size $file.sha256
    }
    return [pscustomobject]@{ result='PASS'; files=45; PE=18; licenses=23; binaryReleaseApproved=$false; publicDistributionAllowed=$false }
}

function Assert-S1BinaryZipLayout([byte[]]$Bytes, $Profile) {
    # Only the canonical ZIP layout written below is accepted: no preamble,
    # extras, comments, encryption, descriptors, links, padding or trailing data.
    if ($Bytes.Length -lt 22) { Reject-S1Binary ZipLayout }
    $end = $Bytes.Length - 22
    if ([BitConverter]::ToUInt32($Bytes,$end) -ne 0x06054b50 -or
        [BitConverter]::ToUInt16($Bytes,$end+4) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$end+6) -ne 0 -or
        [BitConverter]::ToUInt16($Bytes,$end+8) -ne 45 -or [BitConverter]::ToUInt16($Bytes,$end+10) -ne 45 -or
        [BitConverter]::ToUInt16($Bytes,$end+20) -ne 0) { Reject-S1Binary ZipLayout }
    [long]$central = [BitConverter]::ToUInt32($Bytes,$end+16)
    [long]$centralSize = [BitConverter]::ToUInt32($Bytes,$end+12)
    if ($central + $centralSize -ne $end) { Reject-S1Binary ZipLayout }
    [long]$cursor = $central
    [long]$local = 0
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $utf8 = [Text.UTF8Encoding]::new($false,$true)
    for ($i=0; $i -lt 45; $i++) {
        if ($cursor + 46 -gt $end -or [BitConverter]::ToUInt32($Bytes,$cursor) -ne 0x02014b50) { Reject-S1Binary ZipLayout }
        $nameLength = [BitConverter]::ToUInt16($Bytes,$cursor+28)
        $flags = [BitConverter]::ToUInt16($Bytes,$cursor+8)
        $method = [BitConverter]::ToUInt16($Bytes,$cursor+10)
        [long]$compressed = [BitConverter]::ToUInt32($Bytes,$cursor+20)
        if ($cursor+46+$nameLength -gt $end -or $nameLength -eq 0 -or $flags -notin @(0,2048) -or $method -notin @(0,8) -or
            [BitConverter]::ToUInt16($Bytes,$cursor+30) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$cursor+32) -ne 0 -or
            [BitConverter]::ToUInt16($Bytes,$cursor+34) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$cursor+36) -ne 0 -or
            [BitConverter]::ToUInt32($Bytes,$cursor+38) -ne 32 -or
            [BitConverter]::ToUInt32($Bytes,$cursor+42) -ne $local) { Reject-S1Binary ZipMetadata }
        $name = $utf8.GetString($Bytes,$cursor+46,$nameLength)
        Assert-S1RelativePath $name
        if (!$names.Add($name) -or $name -cne $Profile.files[$i].path) { Reject-S1Binary ZipFiles }
        if ($local+30+$nameLength+$compressed -gt $central -or [BitConverter]::ToUInt32($Bytes,$local) -ne 0x04034b50 -or
            [BitConverter]::ToUInt16($Bytes,$local+6) -ne $flags -or [BitConverter]::ToUInt16($Bytes,$local+8) -ne $method -or
            [BitConverter]::ToUInt16($Bytes,$local+26) -ne $nameLength -or [BitConverter]::ToUInt16($Bytes,$local+28) -ne 0 -or
            $utf8.GetString($Bytes,$local+30,$nameLength) -cne $name) { Reject-S1Binary ZipLayout }
        # Time/date, CRC and sizes must agree in both records.
        for ($field=0; $field -lt 16; $field++) {
            if ($Bytes[$local+10+$field] -ne $Bytes[$cursor+12+$field]) { Reject-S1Binary ZipLayout }
        }
        if ([BitConverter]::ToUInt32($Bytes,$cursor+24) -ne $Profile.files[$i].size) { Reject-S1Binary ZipSize }
        $local += 30 + $nameLength + $compressed
        $cursor += 46 + $nameLength
    }
    if ($cursor -ne $end -or $local -ne $central) { Reject-S1Binary ZipLayout }
}
function Read-S1BinaryZip($ZipPath, $Profile) {
    $ZipPath = Get-S1BinaryAbsolute $ZipPath
    $max = [long](($Profile.files | Measure-Object size -Sum).Sum) + 1048576
    if (![IO.File]::Exists($ZipPath) -or [IO.FileInfo]::new($ZipPath).Length -gt $max) { Reject-S1Binary ZipSize }
    $bytes = [IO.File]::ReadAllBytes($ZipPath)
    Assert-S1BinaryZipLayout $bytes $Profile
    $memory = [IO.MemoryStream]::new($bytes,$false)
    $archive = [IO.Compression.ZipArchive]::new($memory,[IO.Compression.ZipArchiveMode]::Read,$false)
    $contents = [Collections.Generic.Dictionary[string,byte[]]]::new([StringComparer]::Ordinal)
    try {
        if ($archive.Entries.Count -ne 45) { Reject-S1Binary ZipFiles }
        for ($i=0; $i -lt 45; $i++) {
            $entry = $archive.Entries[$i]; $expected = $Profile.files[$i]
            if ($entry.FullName -cne $expected.path -or $entry.Length -ne $expected.size) { Reject-S1Binary ZipFiles }
            $input = $entry.Open()
            try {
                $data = [byte[]]::new([int]$expected.size)
                $offset = 0
                while ($offset -lt $data.Length) {
                    $read = $input.Read($data,$offset,$data.Length-$offset)
                    if (!$read) { Reject-S1Binary ZipSize }; $offset += $read
                }
                if ($input.ReadByte() -ne -1 -or (Get-S1BinaryHash $data) -cne $expected.sha256) { Reject-S1Binary ZipHash }
                $contents.Add($entry.FullName,$data)
            } finally { $input.Dispose() }
        }
    } finally { $archive.Dispose(); $memory.Dispose() }
    return [pscustomobject]@{ contents=$contents; size=$bytes.Length; sha256=(Get-S1BinaryHash $bytes) }
}
function Test-S1BinaryZip($Root, $EvidenceRoot, $ZipPath) {
    $plan = Read-S1BinaryPlan $Root $EvidenceRoot
    $zip = Read-S1BinaryZip $ZipPath $plan.profile
    return [pscustomobject]@{ result='PASS'; files=45; size=$zip.size; sha256=$zip.sha256; binaryReleaseApproved=$false; publicDistributionAllowed=$false }
}
function New-S1BinaryDirectory($Path) {
    Assert-S1NoReparse $Path
    if (Test-Path -LiteralPath $Path) { Reject-S1Binary ExistingOutput }
    # Atomic directory claim: no adopting even an empty directory created by a peer.
    $claim = [IO.DirectoryInfo]::new($Path + '.claim-' + [guid]::NewGuid().ToString('N'))
    if (![IO.Directory]::Exists($claim.Parent.FullName)) { Reject-S1Binary ParentMissing }
    $claim.Create()
    [IO.Directory]::Move($claim.FullName,$Path)
}
function Write-S1BinaryNewFile($Path, [byte[]]$Bytes) {
    $stream = [IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $stream.Write($Bytes,0,$Bytes.Length) } finally { $stream.Dispose() }
}
function Write-S1BinaryJson($Path, $Value) {
    $json = ($Value | ConvertTo-Json -Depth 32).Replace("`r`n","`n") + "`n"
    Write-S1BinaryNewFile $Path ([Text.UTF8Encoding]::new($false).GetBytes($json))
}
function New-S1BinaryCandidate($Root, $EvidenceRoot, $OutputRoot) {
    $Root = Get-S1BinaryAbsolute $Root
    $OutputRoot = Get-S1BinaryAbsolute $OutputRoot
    $build = Resolve-S1Path $Root build
    if (!$OutputRoot.StartsWith($build + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { Reject-S1Binary OutputScope }
    if (Test-Path -LiteralPath $OutputRoot) { Reject-S1Binary ExistingOutput }
    $plan = Read-S1BinaryPlan $Root $EvidenceRoot
    $profile = $plan.profile
    New-S1BinaryDirectory $OutputRoot
    Write-S1BinaryJson (Join-Path $OutputRoot owner.json) ([ordered]@{
        profile=$profile.profileId; state='PRIVATE_OUTPUT_ONLY'; reuseAllowed=$false
        binaryReleaseApproved=$false; publicDistributionAllowed=$false; sourceRevision=$profile.sourceRevision
    })
    $payload = Join-Path $OutputRoot payload
    New-S1BinaryDirectory $payload
    foreach ($file in $profile.files) {
        $dest = Resolve-S1Path $payload $file.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        [IO.File]::Copy((Resolve-S1Path $Root $file.source),$dest,$false)
    }
    $null = Test-S1BinaryTree $Root $EvidenceRoot $payload
    $zipPath = Join-Path $OutputRoot $profile.zipName
    $stream = [IO.File]::Open($zipPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $archive = [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try {
        foreach ($file in $profile.files) {
            $entry = $archive.CreateEntry($file.path,[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [DateTimeOffset]::Parse($profile.zipEntryDate,[Globalization.CultureInfo]::InvariantCulture)
            $entry.ExternalAttributes = 32
            $output = $entry.Open()
            $input = [IO.File]::OpenRead((Resolve-S1Path $payload $file.path))
            try { $input.CopyTo($output) } finally { $input.Dispose(); $output.Dispose() }
        }
    } finally { $archive.Dispose(); $stream.Dispose() }
    # Validate the complete ZIP before writing a single expanded entry.
    $zip = Read-S1BinaryZip $zipPath $profile
    $expanded = Join-Path $OutputRoot expanded
    New-S1BinaryDirectory $expanded
    foreach ($file in $profile.files) {
        $dest = Resolve-S1Path $expanded $file.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        Write-S1BinaryNewFile $dest $zip.contents[$file.path]
    }
    $null = Test-S1BinaryTree $Root $EvidenceRoot $expanded
    $inventory = @($profile.files | ForEach-Object { [ordered]@{path=$_.path;size=$_.size;sha256=$_.sha256} })
    $inventoryText = (($inventory | ForEach-Object { "{0}`t{1}`t{2}" -f $_.path,$_.size,$_.sha256 }) -join "`n") + "`n"
    $receipt = [ordered]@{
        result='PASS'; verdict='PASS WITH LIMITATIONS'; purpose='PRIVATE_VALIDATION_ONLY'
        sourceRevision=$profile.sourceRevision; profileSha256=$script:ProfileSha; PE=18; licenses=23; files=45
        zip=[ordered]@{path=$profile.zipName;size=$zip.size;sha256=$zip.sha256}
        inventoryEncoding='UTF-8; path TAB decimal-size TAB uppercase-SHA256 LF; final LF'
        inventorySha256=(Get-S1BinaryHash ([Text.UTF8Encoding]::new($false).GetBytes($inventoryText)))
        inventory=$inventory; productHashesUnchanged=$true; binaryReleaseApproved=$false
        publicDistributionAllowed=$false; openRights=$profile.openRights
    }
    # Success receipt is last. A partial directory/ZIP never constitutes acceptance.
    Write-S1BinaryJson (Join-Path $OutputRoot acceptance.json) $receipt
    return $receipt
}
Export-ModuleMember -Function Read-S1BinaryProfile,Read-S1BinaryPlan,Test-S1BinaryTree,Test-S1BinaryZip,New-S1BinaryCandidate
