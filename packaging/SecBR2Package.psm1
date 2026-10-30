#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Separate 0.1.1 candidate contract. No old creation gate or Owner approval is reused.
. (Join-Path $PSScriptRoot ../tools/ShippingProfileSecBR2.ps1)
$script:BR2PackageSha='D40B66640F03E6ADFD492E9DE26991AC139D482719ABDC7FE2AC917C91107217'
function Write-BR2Bytes($Path,[byte[]]$Bytes){
    Assert-S1NoReparse $Path
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($Bytes,0,$Bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
function Get-BR2ArchiveAbsolute($Path){
    if(![IO.Path]::IsPathFullyQualified($Path)){Stop-S1 BR2ZipPath}
    Assert-S1NoReparse $Path
    return [IO.Path]::GetFullPath($Path)
}
function Get-BR2PayloadBytes($Root,$File,$Profile){
    $bytes=[IO.File]::ReadAllBytes((Resolve-S1Path $Root $File.source))
    if($File.rendered){
        $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
        if([regex]::Matches($text,'\{\{BUILD_SOURCE_COMMIT\}\}').Count -ne 1){Stop-S1 BR2Template}
        $bytes=[Text.Encoding]::UTF8.GetBytes($text.Replace('{{BUILD_SOURCE_COMMIT}}',$Profile.productSourceCommit))
    }
    return ,$bytes
}
function Read-BR2PackageProfile($Root){
    $file=Resolve-S1Path $Root 'packaging/public/profile-sec-br2-v1.json'
    if((Get-S1Hash ([IO.File]::ReadAllBytes($file))) -cne $script:BR2PackageSha){Stop-S1 BR2PackageProfile}
    $p=Read-S1StrictJson $file
    Assert-S1Value $p.profileId 'clipss2-sec-br2-package-v1' BR2PackageProfile
    Assert-S1Value $p.version '0.1.1' BR2PackageProfile
    foreach($key in @('binaryReleaseApproved','publicDistributionAllowed','sourceApprovalInherited')){
        if($p[$key] -isnot [bool] -or $p[$key]){Stop-S1 BR2Approval}
    }
    $plan=Read-BR2SourceInputs $Root
    $null=Test-BR2Product $Root $plan
    Assert-S1Value $p.inputSha256 $plan.sha256 BR2PackageInputs
    foreach($binding in @($p.sourceBindings)+@($p.buildReceipt)){
        Assert-S1Identity (Resolve-S1Path $Root $binding.path) $binding.size $binding.sha256
    }
    $build=Read-S1StrictJson (Resolve-S1Path $Root $p.buildReceipt.path)
    Assert-S1Value $build.result 'PASS' BR2Build
    Assert-S1Value $build.sourceCommit $p.productSourceCommit BR2Build
    Assert-S1Value $build.inputSha256 $p.inputSha256 BR2Build
    foreach($binding in $build.compiledSourceFiles){Assert-S1Identity (Resolve-S1Path $Root $binding.path) $binding.size $binding.sha256}
    if($p.files -isnot [array] -or $p.files.Count -ne 47){Stop-S1 BR2PackageCount}
    $pe=@($p.files|Where-Object kind -CEQ PE)
    $licenses=@($p.files|Where-Object kind -CEQ license-or-patent)
    if($pe.Count -ne 18 -or $licenses.Count -ne 23){Stop-S1 BR2PackageCount}
    Assert-S1InputSet @($build.binaries.path) @($pe.path)
    foreach($item in $pe){
        $known=@($build.binaries|Where-Object path -CEQ $item.path)[0]
        if($item.source -cne ($plan.inputs.productRoot+'/'+$item.path) -or $item.sha256 -cne $known.sha256 -or $item.size -ne $known.size){Stop-S1 BR2PEBinding}
    }
    $old=Read-S1StrictJson (Resolve-S1Path $Root 'packaging/public/profile-s1-binary-release-v4.json')
    $oldLicenses=@($old.files|Where-Object kind -CEQ license-or-patent)
    Assert-S1InputSet @($oldLicenses.path) @($licenses.path)
    foreach($item in $licenses){
        $known=@($oldLicenses|Where-Object path -CEQ $item.path)[0]
        if($item.sha256 -cne $known.sha256 -or $item.size -ne $known.size -or $item.source -cne $known.source){Stop-S1 BR2LicenseBinding}
    }
    foreach($item in $p.files){
        Assert-S1RelativePath $item.path
        $bytes=Get-BR2PayloadBytes $Root $item $p
        if($bytes.Length -ne $item.size -or (Get-S1Hash $bytes) -cne $item.sha256){Stop-S1 BR2PayloadHash}
    }
    $terms=[IO.File]::ReadAllText((Resolve-S1Path $Root 'packaging/sec-br2/MICROSOFT-COMPONENT-TERMS.txt'))
    $prior=[IO.File]::ReadAllText((Resolve-S1Path $Root 'packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt'))
    if($terms -cne $prior.Replace('clipss2 0.1.0 — Windows x64 Portable','clipss2 0.1.1 — Windows x64 Portable')){Stop-S1 BR2TermsChanged}
    foreach($name in @('README.md','THIRD-PARTY-NOTICES.md')){
        $entry=@($p.files|Where-Object path -CEQ $name)[0]
        $text=[Text.Encoding]::UTF8.GetString((Get-BR2PayloadBytes $Root $entry $p))
        foreach($link in [regex]::Matches($text,'\]\(([^)]+)\)')){
            $target=$link.Groups[1].Value
            if($target.StartsWith('https://',[StringComparison]::Ordinal)){continue}
            if($target -cnotin $p.files.path){Stop-S1 BR2DocumentLink}
        }
    }
    return $p
}
function Assert-BR2Tree($Path,$Profile){
    $files=Get-S1Files $Path
    Assert-S1InputSet @($Profile.files.path) @($files.Keys)
    Assert-S1DirectorySet $Path @($Profile.files.path)
    foreach($item in $Profile.files){Assert-S1Identity $files[$item.path] $item.size $item.sha256}
}
function Reject-BR2Archive($Reason){throw ('S1:BR2:Zip:'+ $Reason)}
function Assert-Sdk2Tree($Path,$Profile){
    Assert-S1NoticeTree $Path @($Profile.files.path)
    foreach($file in $Profile.files){Assert-S1Identity (Resolve-S1Path $Path $file.path) $file.size $file.sha256}
}
function Assert-BR2ZipLayout([byte[]]$Bytes, $Profile) {
    # Only the canonical ZIP layout written below is accepted: no preamble,
    # extras, comments, encryption, descriptors, links, padding or trailing data.
    if ($Bytes.Length -lt 22) { Reject-BR2Archive ZipLayout }
    $end = $Bytes.Length - 22
    if ([BitConverter]::ToUInt32($Bytes,$end) -ne 0x06054b50 -or
        [BitConverter]::ToUInt16($Bytes,$end+4) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$end+6) -ne 0 -or
        [BitConverter]::ToUInt16($Bytes,$end+8) -ne $Profile.files.Count -or [BitConverter]::ToUInt16($Bytes,$end+10) -ne $Profile.files.Count -or
        [BitConverter]::ToUInt16($Bytes,$end+20) -ne 0) { Reject-BR2Archive ZipLayout }
    [long]$central = [BitConverter]::ToUInt32($Bytes,$end+16)
    [long]$centralSize = [BitConverter]::ToUInt32($Bytes,$end+12)
    if ($central + $centralSize -ne $end) { Reject-BR2Archive ZipLayout }
    [long]$cursor = $central
    [long]$local = 0
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $utf8 = [Text.UTF8Encoding]::new($false,$true)
    for ($i=0; $i -lt $Profile.files.Count; $i++) {
        if ($cursor + 46 -gt $end -or [BitConverter]::ToUInt32($Bytes,$cursor) -ne 0x02014b50) { Reject-BR2Archive ZipLayout }
        $nameLength = [BitConverter]::ToUInt16($Bytes,$cursor+28)
        $flags = [BitConverter]::ToUInt16($Bytes,$cursor+8)
        $method = [BitConverter]::ToUInt16($Bytes,$cursor+10)
        [long]$compressed = [BitConverter]::ToUInt32($Bytes,$cursor+20)
        if ($cursor+46+$nameLength -gt $end -or $nameLength -eq 0 -or $flags -notin @(0,2048) -or $method -notin @(0,8) -or
            [BitConverter]::ToUInt16($Bytes,$cursor+30) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$cursor+32) -ne 0 -or
            [BitConverter]::ToUInt16($Bytes,$cursor+34) -ne 0 -or [BitConverter]::ToUInt16($Bytes,$cursor+36) -ne 0 -or
            [BitConverter]::ToUInt32($Bytes,$cursor+38) -ne 32 -or
            [BitConverter]::ToUInt32($Bytes,$cursor+42) -ne $local) { Reject-BR2Archive ZipMetadata }
        $name = $utf8.GetString($Bytes,$cursor+46,$nameLength)
        Assert-S1RelativePath $name
        if (!$names.Add($name) -or $name -cne $Profile.files[$i].path) { Reject-BR2Archive ZipFiles }
        if ($local+30+$nameLength+$compressed -gt $central -or [BitConverter]::ToUInt32($Bytes,$local) -ne 0x04034b50 -or
            [BitConverter]::ToUInt16($Bytes,$local+6) -ne $flags -or [BitConverter]::ToUInt16($Bytes,$local+8) -ne $method -or
            [BitConverter]::ToUInt16($Bytes,$local+26) -ne $nameLength -or [BitConverter]::ToUInt16($Bytes,$local+28) -ne 0 -or
            $utf8.GetString($Bytes,$local+30,$nameLength) -cne $name) { Reject-BR2Archive ZipLayout }
        # Time/date, CRC and sizes must agree in both records.
        for ($field=0; $field -lt 16; $field++) {
            if ($Bytes[$local+10+$field] -ne $Bytes[$cursor+12+$field]) { Reject-BR2Archive ZipLayout }
        }
        if ([BitConverter]::ToUInt32($Bytes,$cursor+24) -ne $Profile.files[$i].size) { Reject-BR2Archive ZipSize }
        $local += 30 + $nameLength + $compressed
        $cursor += 46 + $nameLength
    }
    if ($cursor -ne $end -or $local -ne $central) { Reject-BR2Archive ZipLayout }
}
function Read-BR2Archive($ZipPath, $Profile) {
    $ZipPath = Get-BR2ArchiveAbsolute $ZipPath
    [long]$max = 1048576
    foreach($item in $Profile.files){$max += [long]$item.size}
    if (![IO.File]::Exists($ZipPath) -or [IO.FileInfo]::new($ZipPath).Length -gt $max) { Reject-BR2Archive ZipSize }
    $bytes = [IO.File]::ReadAllBytes($ZipPath)
    Assert-BR2ZipLayout $bytes $Profile
    $memory = [IO.MemoryStream]::new($bytes,$false)
    $archive = [IO.Compression.ZipArchive]::new($memory,[IO.Compression.ZipArchiveMode]::Read,$false)
    $contents = [Collections.Generic.Dictionary[string,byte[]]]::new([StringComparer]::Ordinal)
    try {
        if ($archive.Entries.Count -ne $Profile.files.Count) { Reject-BR2Archive ZipFiles }
        for ($i=0; $i -lt $Profile.files.Count; $i++) {
            $entry = $archive.Entries[$i]; $expected = $Profile.files[$i]
            if($entry.LastWriteTime.DateTime -ne [DateTimeOffset]::Parse($Profile.zipTimestamp).DateTime){Reject-BR2Archive ZipMetadata}
            if ($entry.FullName -cne $expected.path -or $entry.Length -ne $expected.size) { Reject-BR2Archive ZipFiles }
            $input = $entry.Open()
            try {
                $data = [byte[]]::new([int]$expected.size)
                $offset = 0
                while ($offset -lt $data.Length) {
                    $read = $input.Read($data,$offset,$data.Length-$offset)
                    if (!$read) { Reject-BR2Archive ZipSize }; $offset += $read
                }
                if ($input.ReadByte() -ne -1 -or (Get-S1Hash $data) -cne $expected.sha256) { Reject-BR2Archive ZipHash }
                $contents.Add($entry.FullName,$data)
            } finally { $input.Dispose() }
        }
    } finally { $archive.Dispose(); $memory.Dispose() }
    return [pscustomobject]@{ contents=$contents; size=$bytes.Length; sha256=(Get-S1Hash $bytes) }
}

function Write-BR2Archive($Path,$PackageRoot,$Profile){
    Assert-BR2Tree $PackageRoot $Profile
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
    try{foreach($item in $Profile.files){
        $entry=$zip.CreateEntry($item.path,[IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime=[DateTimeOffset]::Parse($Profile.zipTimestamp)
        $entry.ExternalAttributes=32
        $out=$entry.Open();$input=[IO.File]::OpenRead((Resolve-S1Path $PackageRoot $item.path))
        try{$input.CopyTo($out)}finally{$input.Dispose();$out.Dispose()}
    }}finally{$zip.Dispose();$stream.Dispose()}
}
function New-BR2Candidate($Root){
    $p=Read-BR2PackageProfile $Root
    $candidate=Resolve-S1Path $Root 'build/sec-br2/candidate'
    if(Test-Path -LiteralPath $candidate){Stop-S1 BR2CandidateExists}
    New-S1OwnedDirectory $candidate
    Write-S1NewJson (Join-Path $candidate owner.json) ([ordered]@{phase='SEC-BR2';state='LOCAL_CANDIDATE_ONLY';profileSha256=$script:BR2PackageSha;reuseAllowed=$false;binaryReleaseApproved=$false})
    $payload=Join-Path $candidate payload
    New-S1OwnedDirectory $payload
    foreach($item in $p.files){
        $dest=Resolve-S1Path $payload $item.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        Write-BR2Bytes $dest (Get-BR2PayloadBytes $Root $item $p)
    }
    Assert-BR2Tree $payload $p
    $zipPath=Join-Path $candidate $p.zipName
    Write-BR2Archive $zipPath $payload $p
    $zip=Read-BR2Archive $zipPath $p
    $expanded=Join-Path $candidate expanded
    New-S1OwnedDirectory $expanded
    foreach($item in $p.files){
        $dest=Resolve-S1Path $expanded $item.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        Write-BR2Bytes $dest $zip.contents[$item.path]
    }
    Assert-BR2Tree $expanded $p
    $null=Read-BR2PackageProfile $Root
    $receipt=[ordered]@{schemaVersion=1;result='PASS';profileSha256=$script:BR2PackageSha;version='0.1.1';
        productSourceCommit=$p.productSourceCommit;buildId=$p.buildId;files=47;PECount=18;licenseCount=23;
        zipName=$p.zipName;zipSize=$zip.size;zipSha256=$zip.sha256;binaryReleaseApproved=$false;publicDistributionAllowed=$false;ownerAcceptance='PENDING'}
    Write-S1NewJson (Join-Path $candidate acceptance.json) $receipt
    return $receipt
}
function Test-BR2Candidate($Root){
    $p=Read-BR2PackageProfile $Root
    $candidate=Resolve-S1Path $Root 'build/sec-br2/candidate'
    $owner=Read-S1StrictJson (Join-Path $candidate owner.json)
    Assert-S1Fields $owner @('phase','state','profileSha256','reuseAllowed','binaryReleaseApproved')
    if($owner.phase -cne 'SEC-BR2' -or $owner.state -cne 'LOCAL_CANDIDATE_ONLY' -or $owner.profileSha256 -cne $script:BR2PackageSha -or $owner.reuseAllowed -isnot [bool] -or $owner.reuseAllowed -or $owner.binaryReleaseApproved -isnot [bool] -or $owner.binaryReleaseApproved){Stop-S1 BR2CandidateOwner}
    foreach($dir in @('payload','expanded')){Assert-BR2Tree (Join-Path $candidate $dir) $p}
    $zip=Read-BR2Archive (Join-Path $candidate $p.zipName) $p
    $receipt=Read-S1StrictJson (Join-Path $candidate acceptance.json)
    Assert-S1Fields $receipt @('schemaVersion','result','profileSha256','version','productSourceCommit','buildId','files','PECount','licenseCount','zipName','zipSize','zipSha256','binaryReleaseApproved','publicDistributionAllowed','ownerAcceptance')
    Assert-BR1Integer $receipt.schemaVersion 1 BR2CandidateReceipt
    Assert-BR1Integer $receipt.files 47 BR2CandidateReceipt
    Assert-BR1Integer $receipt.PECount 18 BR2CandidateReceipt
    Assert-BR1Integer $receipt.licenseCount 23 BR2CandidateReceipt
    if($receipt.version -cne '0.1.1' -or $receipt.buildId -cne $p.buildId -or $receipt.zipName -cne $p.zipName -or $receipt.ownerAcceptance -cne 'PENDING'){Stop-S1 BR2CandidateReceipt}
    if($receipt.result -cne 'PASS' -or $receipt.profileSha256 -cne $script:BR2PackageSha -or $receipt.zipSha256 -cne $zip.sha256 -or $receipt.zipSize -ne $zip.size -or $receipt.productSourceCommit -cne $p.productSourceCommit -or $receipt.binaryReleaseApproved -isnot [bool] -or $receipt.binaryReleaseApproved -or $receipt.publicDistributionAllowed -isnot [bool] -or $receipt.publicDistributionAllowed){Stop-S1 BR2CandidateReceipt}
    return $receipt
}
Export-ModuleMember -Function Read-BR2PackageProfile,Read-BR2Archive,Write-BR2Archive,Assert-BR2Tree,New-BR2Candidate,Test-BR2Candidate
