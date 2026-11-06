#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Only the generic canonical-ZIP/tree primitives are reused, never BR2 profile admission.
Import-Module (Join-Path $PSScriptRoot SecBR2Package.psm1) -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot ../tools/IconV012.psm1) -DisableNameChecking

function Get-IconPayloadBytes($Root,$Item,$Profile) {
    $bytes=[IO.File]::ReadAllBytes((Join-Path $Root $Item.source))
    if($Item.rendered) {
        $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
        if([regex]::Matches($text,'\{\{BUILD_SOURCE_COMMIT\}\}').Count -ne 1){throw 'ICON-012:Template'}
        $bytes=[Text.Encoding]::UTF8.GetBytes($text.Replace('{{BUILD_SOURCE_COMMIT}}',$Profile.productSourceCommit))
    }
    return ,$bytes
}
function New-IconPackageProfile($Root,$RunRoot,$DependencyRoot) {
    $RunRoot=[IO.Path]::GetFullPath($RunRoot)
    $null=Test-IconProduct $Root $RunRoot $DependencyRoot
    $build=Get-Content -LiteralPath (Join-Path $RunRoot BUILD-IDENTITY.json) -Raw|ConvertFrom-Json -AsHashtable
    $runRelative=[IO.Path]::GetRelativePath($Root,$RunRoot).Replace('\','/')
    if(!$runRelative.StartsWith('build/icon-phase3/',[StringComparison]::Ordinal)){throw 'ICON-012:PackageRunRoot'}
    $old=Get-Content -LiteralPath (Join-Path $Root packaging/public/profile-sec-br2-v1.json) -Raw|ConvertFrom-Json -AsHashtable
    $profile=[ordered]@{schemaVersion=1;profileId='clipss2-icon-v012-package-rc1';version='0.1.2';buildId=$build.buildId;
        productSourceCommit=$build.sourceCommit;productSourceTree=$build.sourceTree;inputSha256=$build.inputsSha256;
        runRoot=$runRelative;zipName='clipss2-0.1.2-windows-x64-portable.zip';zipTimestamp='2026-11-06T00:00:00+09:00';
        binaryReleaseApproved=$false;publicDistributionAllowed=$false;ownerAcceptance='PENDING';sourceApprovalInherited=$false;
        sourceBindings=@();files=@()}
    foreach($relative in @('packaging/icon-v012/README.md.in','packaging/icon-v012/THIRD-PARTY-NOTICES.md.in',
        'packaging/icon-v012/MICROSOFT-COMPONENT-TERMS.txt','packaging/IconV012Package.psm1','packaging/SecBR2Package.psm1',
        'tools/package_icon_v012.ps1','LICENSE')) {
        $file=Get-Item -LiteralPath (Join-Path $Root $relative)
        $profile.sourceBindings+=@{path=$relative;size=$file.Length;sha256=(Get-IconHash $file.FullName)}
    }
    foreach($binary in $build.binaries) {
        $profile.files+=@{path=$binary.path;source="$runRelative/product/$($binary.path)";kind='PE';rendered=$false;size=$binary.size;sha256=$binary.sha256}
    }
    foreach($license in @($old.files|Where-Object kind -CEQ 'license-or-patent')) {
        $profile.files+=@{path=$license.path;source=$license.source;kind='license-or-patent';rendered=$false;size=$license.size;sha256=$license.sha256}
    }
    foreach($item in @(
        @('LICENSE','LICENSE',$false),@('README.md','packaging/icon-v012/README.md.in',$true),
        @('THIRD-PARTY-NOTICES.md','packaging/icon-v012/THIRD-PARTY-NOTICES.md.in',$true),
        @('MICROSOFT-COMPONENT-TERMS.txt','packaging/icon-v012/MICROSOFT-COMPONENT-TERMS.txt',$false),
        @('clipss2.portable','packaging/clipss2.portable',$false),
        @('BUILD-IDENTITY.json',"$runRelative/BUILD-IDENTITY.json",$false))) {
        $record=@{path=$item[0];source=$item[1];kind='document';rendered=$item[2]}
        $bytes=Get-IconPayloadBytes $Root $record $profile
        $record.size=$bytes.Length; $record.sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
        $profile.files+=,$record
    }
    $profile.files=@($profile.files|Sort-Object -Property path -CaseSensitive)
    Write-IconJson (Join-Path $Root 'packaging/public/profile-icon-v012-rc1.json') $profile
    return $profile
}
function Read-IconPackageProfile($Root,$DependencyRoot) {
    $path=Join-Path $Root 'packaging/public/profile-icon-v012-rc1.json'
    $p=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json -AsHashtable
    if($p.profileId -cne 'clipss2-icon-v012-package-rc1' -or $p.version -cne '0.1.2' -or
        $p.buildId -cne 'clipss2-0.1.2-icon-rc1' -or $p.zipName -cne 'clipss2-0.1.2-windows-x64-portable.zip' -or
        $p.zipTimestamp -cne '2026-11-06T00:00:00+09:00' -or
        $p.binaryReleaseApproved -isnot [bool] -or $p.binaryReleaseApproved -or
        $p.publicDistributionAllowed -isnot [bool] -or $p.publicDistributionAllowed -or
        $p.sourceApprovalInherited -isnot [bool] -or $p.sourceApprovalInherited -or $p.ownerAcceptance -cne 'PENDING'){throw 'ICON-012:PackageApproval'}
    if(!$p.runRoot.StartsWith('build/icon-phase3/',[StringComparison]::Ordinal) -or $p.runRoot -match '(^/|\\|(^|/)\.\.?(/|$)|:)'){throw 'ICON-012:PackageRunRoot'}
    $null=Test-IconProduct $Root (Join-Path $Root $p.runRoot) $DependencyRoot
    $build=Get-Content -LiteralPath (Join-Path $Root "$($p.runRoot)/BUILD-IDENTITY.json") -Raw|ConvertFrom-Json -AsHashtable
    if($build.sourceCommit -cne $p.productSourceCommit -or $build.sourceTree -cne $p.productSourceTree -or $build.inputsSha256 -cne $p.inputSha256){throw 'ICON-012:PackageBuildBinding'}
    $plan=Read-IconInputs $Root
    $expectedBindings=@($plan.inputs.files|ForEach-Object path)
    $actualBindings=@($build.sourceBindings|ForEach-Object path)
    if($expectedBindings.Count -ne $actualBindings.Count -or @($actualBindings|Sort-Object -Unique).Count -ne $actualBindings.Count -or
        @($actualBindings|Where-Object {$_ -cnotin $expectedBindings}).Count){throw 'ICON-012:BuildSourceClosure'}
    foreach($binding in $build.sourceBindings){$known=@($plan.inputs.files|Where-Object path -CEQ $binding.path)[0];if($binding.sha256 -cne $known.sha256 -or $binding.size -ne $known.size){throw 'ICON-012:BuildSourceBinding'}}
    if($build.sourceCommit -notmatch '^[0-9a-f]{40}$' -or (Get-IconGit $Root @('rev-parse',"$($build.sourceCommit)^{tree}")) -cne $build.sourceTree){throw 'ICON-012:BuildSourceTree'}
    $inputBlob=Get-IconGit $Root @('rev-parse',"$($build.sourceCommit):cmake/icon-v012/source-inputs-v1.json")
    if($inputBlob -cne (Get-IconGit $Root @('hash-object','--no-filters',(Join-Path $Root 'cmake/icon-v012/source-inputs-v1.json')))){throw 'ICON-012:CommittedInputs'}
    $owner=Get-Content -LiteralPath (Join-Path $Root "$($p.runRoot)/owner.json") -Raw|ConvertFrom-Json -AsHashtable
    if($owner.sourceCommit -cne $build.sourceCommit -or $build.binaries.Count -ne 18 -or $build.configuration -cne 'MSVC x64 Release /MD' -or
        $build.runtimeBundled -isnot [bool] -or $build.runtimeBundled -or $build.releaseApproved -isnot [bool] -or $build.releaseApproved -or $build.ownerAcceptance -cne 'PENDING'){throw 'ICON-012:BuildReceipt'}
    if($p.files.Count -ne 47 -or @($p.files|Where-Object kind -CEQ PE).Count -ne 18 -or @($p.files|Where-Object kind -CEQ 'license-or-patent').Count -ne 23){throw 'ICON-012:PackageCount'}
    foreach($binding in $p.sourceBindings) {
        $file=Join-Path $Root $binding.path
        if((Get-Item -LiteralPath $file).Length -ne $binding.size -or (Get-IconHash $file) -cne $binding.sha256){throw 'ICON-012:PackageSourceBinding'}
    }
    $old=Get-Content -LiteralPath (Join-Path $Root packaging/public/profile-sec-br2-v1.json) -Raw|ConvertFrom-Json -AsHashtable
    $expectedNames=@($build.binaries.path)+@($old.files|Where-Object kind -CEQ 'license-or-patent'|ForEach-Object path)+@('LICENSE','README.md','THIRD-PARTY-NOTICES.md','MICROSOFT-COMPONENT-TERMS.txt','clipss2.portable','BUILD-IDENTITY.json')
    $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($item in $p.files){if(!$names.Add($item.path) -or $item.path -cnotin $expectedNames -or $item.rendered -isnot [bool]){throw 'ICON-012:PackageFileSet'}}
    if($names.Count -ne $expectedNames.Count){throw 'ICON-012:PackageFileSet'}
    $documents=@{'LICENSE'=@('LICENSE',$false);'README.md'=@('packaging/icon-v012/README.md.in',$true);
        'THIRD-PARTY-NOTICES.md'=@('packaging/icon-v012/THIRD-PARTY-NOTICES.md.in',$true);
        'MICROSOFT-COMPONENT-TERMS.txt'=@('packaging/icon-v012/MICROSOFT-COMPONENT-TERMS.txt',$false);
        'clipss2.portable'=@('packaging/clipss2.portable',$false);'BUILD-IDENTITY.json'=@("$($p.runRoot)/BUILD-IDENTITY.json",$false)}
    foreach($name in $documents.Keys){$item=@($p.files|Where-Object path -CEQ $name)[0];if($item.source -cne $documents[$name][0] -or $item.rendered -ne $documents[$name][1]){throw 'ICON-012:DocumentBinding'}}
    foreach($known in @($old.files|Where-Object kind -CEQ 'license-or-patent')) {
        $matching=@($p.files|Where-Object path -CEQ $known.path)
        if($matching.Count -ne 1 -or $matching[0].sha256 -cne $known.sha256 -or $matching[0].source -cne $known.source){throw 'ICON-012:LicenseBinding'}
    }
    $oldTerms=[IO.File]::ReadAllText((Join-Path $Root 'packaging/sec-br2/MICROSOFT-COMPONENT-TERMS.txt'))
    $newTerms=[IO.File]::ReadAllText((Join-Path $Root 'packaging/icon-v012/MICROSOFT-COMPONENT-TERMS.txt'))
    if($newTerms -cne $oldTerms.Replace('clipss2 0.1.1 — Windows x64 Portable','clipss2 0.1.2 — Windows x64 Portable')){throw 'ICON-012:TermsBody'}
    foreach($item in $p.files) {
        # The archive primitive also validates names/duplicates before any extraction.
        if($item.path -match '(^/|\\|(^|/)\.\.?(/|$)|:)' -or $item.source -match '(^/|\\|(^|/)\.\.?(/|$)|:)'){throw 'ICON-012:PackagePath'}
        $bytes=Get-IconPayloadBytes $Root $item $p
        if($bytes.Length -ne $item.size -or [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)) -cne $item.sha256){throw 'ICON-012:PayloadHash'}
        if($item.kind -ceq 'PE') {
            $known=@($build.binaries|Where-Object path -CEQ $item.path)
            if($known.Count -ne 1 -or $known[0].sha256 -cne $item.sha256 -or $item.source -cne "$($p.runRoot)/product/$($item.path)"){throw 'ICON-012:PackagePEBinding'}
        }
    }
    return $p
}
function New-IconCandidate($Root,$DependencyRoot) {
    $p=Read-IconPackageProfile $Root $DependencyRoot
    $candidate=Join-Path $Root "$($p.runRoot)/candidate"
    if(Test-Path -LiteralPath $candidate){throw 'ICON-012:CandidateExists'}
    $null=New-Item -ItemType Directory -Path $candidate
    $payload=Join-Path $candidate payload
    $null=New-Item -ItemType Directory -Path $payload
    foreach($item in $p.files) {
        $dest=Join-Path $payload $item.path
        $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        $stream=[IO.File]::Open($dest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$bytes=Get-IconPayloadBytes $Root $item $p;$stream.Write($bytes)}finally{$stream.Dispose()}
    }
    Assert-BR2Tree $payload $p
    $zipPath=Join-Path $candidate $p.zipName
    Write-BR2Archive $zipPath $payload $p
    $zip=Read-BR2Archive $zipPath $p
    $expanded=Join-Path $candidate expanded
    $null=New-Item -ItemType Directory -Path $expanded
    foreach($item in $p.files) {
        $dest=Join-Path $expanded $item.path
        $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        $stream=[IO.File]::Open($dest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$stream.Write($zip.contents[$item.path])}finally{$stream.Dispose()}
    }
    Assert-BR2Tree $expanded $p
    $receipt=[ordered]@{result='PASS';version='0.1.2';profileSha256=(Get-IconHash (Join-Path $Root packaging/public/profile-icon-v012-rc1.json));
        buildSourceCommit=$p.productSourceCommit;buildId=$p.buildId;files=47;PECount=18;licenseCount=23;zipName=$p.zipName;
        zipSize=$zip.size;zipSha256=$zip.sha256;ownerAcceptance='PENDING';binaryReleaseApproved=$false;publicDistributionAllowed=$false}
    Write-IconJson (Join-Path $candidate acceptance.json) $receipt
    return $receipt
}
function Test-IconCandidate($Root,$DependencyRoot) {
    $p=Read-IconPackageProfile $Root $DependencyRoot
    $candidate=Join-Path $Root "$($p.runRoot)/candidate"
    foreach($name in @('payload','expanded')){Assert-BR2Tree (Join-Path $candidate $name) $p}
    $zip=Read-BR2Archive (Join-Path $candidate $p.zipName) $p
    $receipt=Get-Content -LiteralPath (Join-Path $candidate acceptance.json) -Raw|ConvertFrom-Json -AsHashtable
    if($receipt.zipSha256 -cne $zip.sha256 -or $receipt.zipSize -ne $zip.size -or $receipt.profileSha256 -cne (Get-IconHash (Join-Path $Root packaging/public/profile-icon-v012-rc1.json)) -or
        $receipt.result -cne 'PASS' -or $receipt.version -cne '0.1.2' -or $receipt.files -ne 47 -or $receipt.PECount -ne 18 -or $receipt.licenseCount -ne 23 -or
        $receipt.buildSourceCommit -cne $p.productSourceCommit -or $receipt.buildId -cne $p.buildId -or $receipt.zipName -cne $p.zipName -or
        $receipt.binaryReleaseApproved -isnot [bool] -or $receipt.binaryReleaseApproved -or
        $receipt.publicDistributionAllowed -isnot [bool] -or $receipt.publicDistributionAllowed -or $receipt.ownerAcceptance -cne 'PENDING'){throw 'ICON-012:CandidateReceipt'}
    return $receipt
}
Export-ModuleMember -Function Get-IconPayloadBytes,New-IconPackageProfile,Read-IconPackageProfile,New-IconCandidate,Test-IconCandidate
