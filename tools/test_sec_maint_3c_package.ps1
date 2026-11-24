#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('SelfTest','Verify')][string]$Mode,
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][string]$ProductDirectory,
    [Parameter(Mandatory)][string]$PreviousRun
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot IconV012.psm1) -Force -DisableNameChecking
. (Join-Path $PSScriptRoot ShippingProfileSecBR1.ps1)

$root=Split-Path $PSScriptRoot -Parent
$run=[IO.Path]::GetFullPath($RunRoot)
$prefix=(Join-Path $root 'build/sec-maint-v013/3c')+[IO.Path]::DirectorySeparatorChar
if(!$run.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){Stop-S1 Maint3cRunRoot}
Assert-S1NoReparse $run
$installed=Join-Path $run installed
$profilePath=Resolve-S1Path $root 'packaging/public/profile-sec-maint-v013-3c.json'
$ledgerPath=Resolve-S1Path $root 'cmake/security/sec-maint-v013/3c/source-inputs-v1.json'
$plan=Read-S1StrictJson $ledgerPath
$profile=Read-S1StrictJson $profilePath
$set=@('clipss2.exe')+@($profile.productRuntimeDlls)
$sourceHash=(Get-FileHash -LiteralPath $ledgerPath).Hash
$profileHash=(Get-FileHash -LiteralPath $profilePath).Hash
$receiptId='clipss2-sec-maint-3c-install-receipt-v1'
$markerId='clipss2-sec-maint-3c-installed-v1'
$receiptPath=Resolve-S1Path $installed "share/$receiptId.json"
$markerPath=Resolve-S1Path $installed "share/$markerId.json"

function Assert-Maint3cPackage($Product,$Install,$ReceiptFile,$MarkerFile){
    if([IO.Path]::GetFullPath($Install) -cne $installed){Stop-S1 Maint3cInstallRoot}
    $receipt=Read-S1StrictJson $ReceiptFile
    if($receipt.receiptId -cne $receiptId -or $receipt.sourceInputsSha256 -ine $sourceHash){Stop-S1 Maint3cReceipt}
    $marker=Read-S1StrictJson $MarkerFile
    if($marker.markerId -cne $markerId -or $marker.sourceInputsSha256 -ine $sourceHash -or
       $marker.receiptSha256 -ine (Get-FileHash -LiteralPath $ReceiptFile).Hash){Stop-S1 Maint3cMarker}

    $files=Get-S1Files $Product
    if($files.ContainsKey('z.dll')){Stop-S1 Maint3cLegacyZlib}
    if(!$files.ContainsKey('libz.dll')){Stop-S1 Maint3cMissingZlib}
    if($files.Count -ne $set.Count -or @($files.Keys|Where-Object {$_ -cnotin $set}).Count){Stop-S1 Maint3cPayloadNames}
    $packages=$receipt.packages
    $zlib=@($packages|Where-Object {$_.name -ceq 'zlib' -and !$_.feature})
    if($zlib.Count -ne 1 -or $zlib[0].version -cne $plan.zlib.version -or
       $zlib[0].portVersion -ne $plan.zlib.portVersion -or $zlib[0].triplet -cne $plan.targetTriplet){
        Stop-S1 Maint3cZlibIdentity
    }
    foreach($name in $set){
        $pe=Read-IconPE $files[$name]
        if('z.dll' -in $pe.imports){Stop-S1 Maint3cLegacyImport}
        if($name -ceq 'libpng16.dll' -and 'libz.dll' -notin $pe.imports){Stop-S1 Maint3cPngImport}
        if($name -ceq 'clipss2.exe'){continue}
        $bound=@($receipt.inventory|Where-Object path -CEQ ($plan.targetTriplet+'/bin/'+$name))
        if($bound.Count -ne 1 -or $pe.size -ne $bound[0].size -or $pe.sha256 -cne $bound[0].sha256){
            Stop-S1 Maint3cDllHash
        }
    }
}

if($Mode -ceq 'SelfTest'){
    $scratch=Join-Path $run ('build/packaging-tests/'+[guid]::NewGuid().ToString('N'))
    New-S1OwnedDirectory $scratch
    $oldInstall=Resolve-S1Path $PreviousRun 'installed'
    $oldProduct=Resolve-S1Path $PreviousRun 'product'
    $oldReceipt=Resolve-S1Path $oldInstall 'share/clipss2-sec-maint-3b-install-receipt-v1.json'
    $oldMarker=Resolve-S1Path $oldInstall 'share/clipss2-sec-maint-3b-installed-v1.json'

    function New-Maint3cFixture($Name,$Skip=@(),$Replace=@{},$Add=@{}){
        $fixture=Join-Path $scratch $Name
        New-S1OwnedDirectory $fixture
        foreach($name in $set){
            if($name -cin $Skip){continue}
            $file=if($Replace.ContainsKey($name)){$Replace[$name]}else{Resolve-S1Path $ProductDirectory $name}
            [IO.File]::Copy($file,(Join-Path $fixture $name),$false)
        }
        foreach($entry in $Add.GetEnumerator()){
            [IO.File]::Copy($entry.Value,(Join-Path $fixture $entry.Key),$false)
        }
        return $fixture
    }
    function Assert-Maint3cRejected($Label,$Expected,$Product,$InstallRoot=$installed,$ReceiptFile=$receiptPath,$MarkerFile=$markerPath){
        try{Assert-Maint3cPackage $Product $InstallRoot $ReceiptFile $MarkerFile}
        catch{
            if($_.Exception.Message -cne ('S1:'+$Expected)){throw}
            "$Label $Expected"
            return
        }
        Stop-S1 ('Maint3cAccepted:'+ $Label)
    }
    $legacy=Resolve-S1Path $oldProduct 'z.dll'
    $legacyPng=Resolve-S1Path $oldProduct 'libpng16.dll'
    $candidateZlib=Resolve-S1Path $ProductDirectory 'libz.dll'
    $good=New-Maint3cFixture good
    Assert-Maint3cPackage $good $installed $receiptPath $markerPath
    Assert-Maint3cRejected 'missing libz' Maint3cMissingZlib (New-Maint3cFixture missing @('libz.dll'))
    Assert-Maint3cRejected 'old DLL substitution' Maint3cLegacyZlib (New-Maint3cFixture legacy @('libz.dll') @{} @{'z.dll'=$legacy})
    Assert-Maint3cRejected 'both DLLs' Maint3cLegacyZlib (New-Maint3cFixture mixed @() @{} @{'z.dll'=$legacy})
    $wrong=New-Maint3cFixture wrong-digest
    $stream=[IO.File]::Open((Join-Path $wrong 'libz.dll'),[IO.FileMode]::Append)
    try{$stream.WriteByte(1)}finally{$stream.Dispose()}
    Assert-Maint3cRejected 'same name with other bytes' Maint3cDllHash $wrong
    Assert-Maint3cRejected 'legacy import' Maint3cLegacyImport (New-Maint3cFixture old-import @() @{'libpng16.dll'=$legacyPng})
    Assert-Maint3cRejected 'old receipt' Maint3cReceipt $good $installed $oldReceipt $markerPath
    Assert-Maint3cRejected 'old marker' Maint3cMarker $good $installed $receiptPath $oldMarker
    Assert-Maint3cRejected 'extra DLL' Maint3cPayloadNames (New-Maint3cFixture extra @() @{} @{'extra.dll'=$candidateZlib})
    Assert-Maint3cRejected 'other installation' Maint3cInstallRoot $good $oldInstall
    return
}

Assert-Maint3cPackage $ProductDirectory $installed $receiptPath $markerPath
$identityPath=Resolve-S1Path $run 'BUILD-IDENTITY.json'
$identity=Read-S1StrictJson $identityPath
if($identity.scope -cne 'sec-maint-v013-3c' -or $identity.sourceInputsSha256 -cne $sourceHash -or
   $identity.profileSha256 -cne $profileHash -or
   $identity.installedReceiptSha256 -cne (Get-FileHash -LiteralPath $receiptPath).Hash -or
   $identity.installedMarkerSha256 -cne (Get-FileHash -LiteralPath $markerPath).Hash){Stop-S1 Maint3cBuildIdentity}
Assert-S1InputSet $set @($identity.binaries.path)
foreach($file in $identity.binaries){
    Assert-S1Identity (Resolve-S1Path $ProductDirectory $file.path) $file.size $file.sha256
}

$components=@(foreach($package in $plan.packages){
    $item=[ordered]@{type='library';'bom-ref'=('vcpkg:'+$package.name);name=$package.name;version=$package.version;
        properties=@(@{name='clipss2:role';value=$package.role},@{name='clipss2:vcpkgPortTree';value=$package.portTree})}
    if($package.name -ceq 'zlib'){
        $item.licenses=@(@{license=@{id='Zlib'}})
        $item.properties+=@(@{name='clipss2:archiveSha512';value=$plan.zlib.archiveSha512},
            @{name='clipss2:linkage';value='shared'},@{name='clipss2:licenseFile';value=$plan.zlib.license})
        $item.externalReferences=@(@{type='vcs';url=$plan.zlib.source})
    }
    if($package.name -ceq 'libjpeg-turbo'){
        $item.components=@(foreach($input in $plan.embedded){
            [ordered]@{type='library';'bom-ref'=('turbojpeg:embedded:'+$input.name);name=$input.name;version=$input.version;
                licenses=@(@{expression=$input.licenseExpression});externalReferences=@(@{type='vcs';url=$input.source});
                properties=@(@{name='clipss2:container';value=$input.container},
                    @{name='clipss2:linkage';value='static'},@{name='clipss2:licenseFile';value=$input.licenseFile})}
        })
    }
    $item
})
$components+=@(foreach($file in $identity.binaries|Where-Object path -CNE 'clipss2.exe'){
    [ordered]@{type='file';'bom-ref'=('file:'+$file.path);name=$file.path;
        hashes=@(@{alg='SHA-256';content=$file.sha256.ToLowerInvariant()})}
})
$components+=@(foreach($input in $plan.managed){
    [ordered]@{type='library';'bom-ref'=('managed:'+$input.name);name=$input.name;version=$input.commit;
        properties=@(@{name='clipss2:role';value=$input.role})}
})
$exe=$identity.binaries|Where-Object path -CEQ 'clipss2.exe'
$candidateRef='clipss2:sec-maint-v013-3c'
$bom=[ordered]@{bomFormat='CycloneDX';specVersion='1.6';version=1;
    metadata=@{component=@{type='application';'bom-ref'=$candidateRef;name='clipss2';version=$identity.version;
        hashes=@(@{alg='SHA-256';content=$exe.sha256.ToLowerInvariant()})};
        properties=@(@{name='clipss2:sourceInputsSha256';value=$sourceHash},
            @{name='clipss2:profileSha256';value=$profileHash},
            @{name='clipss2:receiptSha256';value=$identity.installedReceiptSha256},
            @{name='clipss2:markerSha256';value=$identity.installedMarkerSha256},
            @{name='clipss2:buildIdentitySha256';value=(Get-FileHash -LiteralPath $identityPath).Hash})};
    components=$components;
    dependencies=@(@{ref=$candidateRef;dependsOn=@($profile.productRuntimeDlls|ForEach-Object {'file:'+$_})},
        @{ref='file:libpng16.dll';dependsOn=@('file:libz.dll')},
        @{ref='vcpkg:libpng';dependsOn=@('vcpkg:zlib')})}
Write-S1NewJson (Join-Path $run 'SBOM.cdx.json') $bom
