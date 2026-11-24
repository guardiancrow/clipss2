#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Seal','Verify')][string]$Mode,
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][string]$ReferenceInstalled,
    [ValidateSet('3a','3b','3c')][string]$Phase='3a',
    [string]$PreviousRun,
    [string]$Root=(Split-Path $PSScriptRoot -Parent)
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ShippingProfileSecBR1.ps1)

function Read-MaintStatus([string]$Installed) {
    $status=[IO.File]::ReadAllText((Resolve-S1Path $Installed 'vcpkg/status'))
    foreach($paragraph in ($status -split '(?:\r?\n){2,}')) {
        if([string]::IsNullOrWhiteSpace($paragraph)){continue}
        $fields=@{}
        foreach($line in ($paragraph -split '\r?\n')) {
            if($line -cmatch '^([^: ]+): (.*)$') {
                if($fields.ContainsKey($Matches[1])){Stop-S1 MaintDuplicateStatus}
                $fields[$Matches[1]]=$Matches[2]
            }
        }
        if($fields['Status'] -cne 'install ok installed'){Stop-S1 MaintInvalidStatus}
        [ordered]@{name=$fields['Package'];version=$fields['Version'];portVersion=[int]$fields['Port-Version'];
            feature=$fields['Feature'];triplet=$fields['Architecture'];depends=$fields['Depends'];abi=$fields['Abi']}
    }
}

$run=[IO.Path]::GetFullPath($RunRoot)
$prefix=(Join-Path ([IO.Path]::GetFullPath($Root)) "build/sec-maint-v013/$Phase")+[IO.Path]::DirectorySeparatorChar
if(!$run.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){Stop-S1 MaintRunRoot}
Assert-S1NoReparse $run
$installed=Join-Path $run installed
$tools=Read-S1StrictJson (Resolve-S1Path $Root 'cmake/security/sec-maint-v013/toolchain-v1.json')
$manifest='cmake/security/sec-maint-v013'
if($Phase -cne '3a'){$manifest+="/$Phase"}
$ledgerPath=Resolve-S1Path $Root "$manifest/source-inputs-v1.json"
$inputs=Read-S1StrictJson $ledgerPath
$inputsHash=(Get-FileHash -LiteralPath $ledgerPath).Hash.ToLowerInvariant()
foreach($binding in $inputs.files) {
    Assert-S1Identity (Resolve-S1Path $Root $binding.path) $binding.size $binding.sha256
}
$archive=Join-Path $run 'downloads/libjxl-libjxl-v0.13.0.tar.gz'
if((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ine $inputs.libjxl.archiveSha512){Stop-S1 MaintArchive}
if($Phase -cne '3a'){
    $pngArchive=Join-Path $run 'downloads/pnggroup-libpng-v1.6.59.tar.gz'
    if((Get-FileHash -LiteralPath $pngArchive -Algorithm SHA512).Hash -ine $inputs.libpng.archiveSha512){Stop-S1 MaintPngArchive}
}
if($Phase -ceq '3c'){
    $zlibArchive=Join-Path $run 'downloads/madler-zlib-v1.4.tar.gz'
    if((Get-FileHash -LiteralPath $zlibArchive -Algorithm SHA512).Hash -ine $inputs.zlib.archiveSha512){Stop-S1 MaintZlibArchive}
}
$vcpkg=Join-Path $run vcpkg
$gitOptions=@('--no-optional-locks','-c',('safe.directory='+$vcpkg.Replace('\','/')),'-C',$vcpkg)
$tool=Join-Path $vcpkg vcpkg.exe
if((Get-FileHash -LiteralPath $tool).Hash -cne $tools.vcpkgToolSha256){Stop-S1 MaintTool}
$commit=& git @gitOptions rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $commit -cne $tools.vcpkgBaseline){Stop-S1 MaintRegistry}
foreach($package in $inputs.packages) {
    if($package.name -cin @('brotli','highway','libjxl','libyuv')){continue}
    if($Phase -cne '3a' -and $package.name -ceq 'libpng'){continue}
    if($Phase -ceq '3c' -and $package.name -ceq 'zlib'){continue}
    $tree=& git @gitOptions rev-parse ("HEAD:ports/"+$package.name)
    if($LASTEXITCODE -ne 0 -or $tree -cne $package.portTree){Stop-S1 MaintRegistryPort}
}

if($Phase -ceq '3a'){
    $oldReceipt=Resolve-S1Path $ReferenceInstalled 'share/clipss2-sec-br1-install-receipt-v1.json'
}else{
    $previousPhase=if($Phase -ceq '3c'){'3b'}else{'3a'}
    if(!$PreviousRun){Stop-S1 MaintPreviousRun}
    $previous=[IO.Path]::GetFullPath($PreviousRun)
    $oldPrefix=(Join-Path ([IO.Path]::GetFullPath($Root)) "build/sec-maint-v013/$previousPhase")+[IO.Path]::DirectorySeparatorChar
    if(!$previous.StartsWith($oldPrefix,[StringComparison]::OrdinalIgnoreCase)){Stop-S1 MaintPreviousRun}
    Assert-S1NoReparse $previous
    $ReferenceInstalled=Join-Path $previous installed
    $oldReceipt=Resolve-S1Path $ReferenceInstalled "share/clipss2-sec-maint-$previousPhase-install-receipt-v1.json"
    $oldMarker=Resolve-S1Path $ReferenceInstalled "share/clipss2-sec-maint-$previousPhase-installed-v1.json"
    $oldIdentity=Resolve-S1Path $previous 'BUILD-IDENTITY.json'
    if((Get-FileHash -LiteralPath $oldMarker).Hash -cne $inputs.referenceMarkerSha256 -or
       (Get-FileHash -LiteralPath $oldIdentity).Hash -cne $inputs.referenceBuildIdentitySha256){Stop-S1 MaintPreviousIdentity}
}
if((Get-FileHash -LiteralPath $oldReceipt).Hash -cne $inputs.referenceReceiptSha256){Stop-S1 MaintReference}
$oldStatus=(Read-S1StrictJson $oldReceipt).inventory | Where-Object path -CEQ 'vcpkg/status'
Assert-S1Identity (Resolve-S1Path $ReferenceInstalled $oldStatus.path) $oldStatus.size $oldStatus.sha256
$prior=@(Read-MaintStatus $ReferenceInstalled)
$actual=@(Read-MaintStatus $installed)
function Get-MaintPackageKey($Row){
    $feature=if($Row.feature){$Row.feature}else{'core'}
    return ($Row.name+'/'+$Row.triplet+'/'+$feature)
}
Assert-S1InputSet @($prior | ForEach-Object {Get-MaintPackageKey $_}) @($actual | ForEach-Object {Get-MaintPackageKey $_})
foreach($entry in $actual){
    $key=Get-MaintPackageKey $entry
    $old=@($prior | Where-Object {(Get-MaintPackageKey $_) -ceq $key})[0]
    $expected=$old.version
    $expectedPortVersion=$old.portVersion
    if($Phase -ceq '3a' -and $entry.name -ceq 'libjxl'){$expected='0.13.0'}
    if($Phase -ceq '3b' -and $entry.name -ceq 'libpng'){
        $expected=$inputs.libpng.version
        $expectedPortVersion=$inputs.libpng.portVersion
    }
    if($Phase -ceq '3c' -and $entry.name -ceq 'zlib'){
        $expected=$inputs.zlib.version
        $expectedPortVersion=$inputs.zlib.portVersion
    }
    if($entry.version -cne $expected -or $entry.portVersion -ne $expectedPortVersion -or
       $entry.depends -cne $old.depends){Stop-S1 MaintResolvedPackages}
    if(!$entry.feature -and $entry.abi -cnotmatch '^[a-f0-9]{64}$'){Stop-S1 MaintAbiMissing}
    if($Phase -ceq '3b' -and !$entry.feature){
        if(($entry.abi -cne $old.abi) -ne ($entry.name -ceq 'libpng')){Stop-S1 MaintUnexpectedAbiChange}
    }
    if($Phase -ceq '3c' -and !$entry.feature -and $entry.name -cin @('zlib','libpng') -and $entry.abi -ceq $old.abi){
        Stop-S1 MaintMissingRebuild
    }
    $entry.priorAbi=$old.abi
}

Test-S1DependencyPayload $installed ([pscustomobject]@{inputs=$inputs})
$bin=Join-Path $installed ($tools.targetTriplet+'/bin')
Assert-S1InputSet @($inputs.installedRuntimeDlls) @((Get-ChildItem -LiteralPath $bin -File -Filter '*.dll').Name)
foreach($name in 'jxl','jxl_cms','jxl_threads'){
    foreach($relative in @("lib/$name.lib","lib/pkgconfig/lib${name}.pc")){
        $file=Resolve-S1Path $installed ($tools.targetTriplet+'/'+$relative)
        if(!(Test-Path -LiteralPath $file -PathType Leaf)){Stop-S1 MaintJxlArtifact}
    }
}
$pkgconf=Resolve-S1Path $installed 'x64-windows/tools/pkgconf/pkgconf.exe'
$oldLibDir=$env:PKG_CONFIG_LIBDIR
$oldPkgPath=$env:PKG_CONFIG_PATH
$pcQueries=[ordered]@{}
try {
    $env:PKG_CONFIG_LIBDIR=Join-Path $installed ($tools.targetTriplet+'/lib/pkgconfig')
    $env:PKG_CONFIG_PATH=$null
    foreach($name in 'libjxl','libjxl_cms','libjxl_threads'){
        $version=& $pkgconf --modversion $name
        if($LASTEXITCODE -ne 0 -or $version -cne '0.13.0'){Stop-S1 MaintJxlPcVersion}
        foreach($query in 'shared','static'){
            $arguments=if($query -ceq 'static'){@('--static','--libs-only-l',$name)}else{@('--libs-only-l',$name)}
            $result=(& $pkgconf @arguments) -join ' '
            if($LASTEXITCODE -ne 0 -or $result -match '(?:^|\s)-lm(?:\s|$)'){Stop-S1 MaintJxlPcLibm}
            $pcQueries["$name/$query"]=$result.Trim()
        }
    }
    if($Phase -ceq '3c'){
        $zlibVersion=& $pkgconf --modversion zlib
        if($LASTEXITCODE -ne 0 -or $zlibVersion -cne $inputs.zlib.version){Stop-S1 MaintZlibPcVersion}
        foreach($query in 'shared','static'){
            $arguments=if($query -ceq 'static'){@('--static','--libs-only-l','zlib')}else{@('--libs-only-l','zlib')}
            $result=(& $pkgconf @arguments) -join ' '
            if($LASTEXITCODE -ne 0 -or $result.Trim() -cne '-llibz'){Stop-S1 MaintZlibPcLink}
            $pcQueries["zlib/$query"]=$result.Trim()
        }
        foreach($relative in 'bin/libz.dll','lib/libz.lib','include/zlib.h','share/zlib/ZLIBConfig.cmake'){
            if(!(Test-Path -LiteralPath (Resolve-S1Path $installed ($tools.targetTriplet+'/'+$relative)) -PathType Leaf)){
                Stop-S1 MaintZlibArtifact
            }
        }
    }
} finally {
    $env:PKG_CONFIG_LIBDIR=$oldLibDir
    $env:PKG_CONFIG_PATH=$oldPkgPath
}

$receiptId="clipss2-sec-maint-$Phase-install-receipt-v1"
$markerId="clipss2-sec-maint-$Phase-installed-v1"
$receiptRelative="share/$receiptId.json"
$markerRelative="share/$markerId.json"
$receiptPath=Resolve-S1Path $installed $receiptRelative
$markerPath=Resolve-S1Path $installed $markerRelative
$files=Get-S1Files $installed
$inventory=@(foreach($relative in ($files.Keys|Sort-Object -CaseSensitive)){
    if($relative -cin @($receiptRelative,$markerRelative)){continue}
    $file=Get-Item -LiteralPath $files[$relative]
    [ordered]@{path=$relative;size=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash}
})
if($Mode -ceq 'Seal'){
    if((Test-Path -LiteralPath $receiptPath) -or (Test-Path -LiteralPath $markerPath)){Stop-S1 MaintAlreadySealed}
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $receiptPath))
    $sealedReceipt=[ordered]@{schemaVersion=1;receiptId=$receiptId;
        sourceInputsSha256=$inputsHash;vcpkgBaseline=$tools.vcpkgBaseline;vcpkgToolSha256=$tools.vcpkgToolSha256;
        compiler=$tools.compiler;packages=$actual;pkgconfig=$pcQueries;inventory=$inventory}
    if($Phase -ceq '3c'){
        $sealedReceipt.zlib=$inputs.zlib
        $sealedReceipt.embedded=$inputs.embedded
        $sealedReceipt.installedRuntimeDlls=$inputs.installedRuntimeDlls
    }
    Write-S1NewJson $receiptPath $sealedReceipt
    Write-S1NewJson $markerPath ([ordered]@{schemaVersion=1;markerId=$markerId;
        sourceInputsSha256=$inputsHash;receiptSha256=(Get-FileHash -LiteralPath $receiptPath).Hash.ToLowerInvariant()})
}
$receipt=Read-S1StrictJson $receiptPath
$marker=Read-S1StrictJson $markerPath
if($receipt.receiptId -cne $receiptId -or $marker.markerId -cne $markerId -or
   $receipt.sourceInputsSha256 -cne $inputsHash -or $marker.sourceInputsSha256 -cne $inputsHash -or
   $marker.receiptSha256 -cne (Get-FileHash -LiteralPath $receiptPath).Hash.ToLowerInvariant()){Stop-S1 MaintReceiptBinding}
Assert-S1InputSet @($inventory.path) @($receipt.inventory.path)
foreach($item in $receipt.inventory){Assert-S1Identity $files[$item.path] $item.size $item.sha256}
[ordered]@{result='PASS';sourceInputsSha256=$inputsHash;receiptSha256=$marker.receiptSha256;
    packageCount=@($actual | Where-Object { !$_.feature }).Count;installedDllCount=$inputs.installedRuntimeDlls.Count;
    pkgconfig=$pcQueries;binaryReleaseApproved=$false}|ConvertTo-Json -Depth 5
