#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][string]$ReferenceInstalled,
    [Parameter(Mandatory)][string]$ReferenceProduct,
    [Parameter(Mandatory)][string]$ManagedOriginal,
    [Parameter(Mandatory)][string]$ManagedDerivative,
    [ValidateSet('3a','3b','3c','3d','3e','3f','3g-b','4a','4b-b')][string]$Phase='3a',
    [string]$PreviousRun,
    [string]$Png3aRun,
    [string]$Png3bRun,
    [string]$TurboReferenceRun,
    [string]$AvifReferenceRun,
    [string]$WebpReferenceRun,
    [ValidateRange(1,32)][int]$Jobs=6
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot IconV012.psm1) -Force -DisableNameChecking
. (Join-Path $PSScriptRoot ShippingProfileSecBR1.ps1)

$root=Split-Path $PSScriptRoot -Parent
$run=[IO.Path]::GetFullPath($RunRoot)
$prefix=(Join-Path $root "build/sec-maint-v013/$Phase")+[IO.Path]::DirectorySeparatorChar
if(!$run.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){Stop-S1 MaintRunRoot}
Assert-S1NoReparse $run
$manifest='cmake/security/sec-maint-v013'
if($Phase -cne '3a'){
    if(!$PreviousRun){Stop-S1 MaintPreviousRun}
    $manifest+="/$Phase"
}
$scope="sec-maint-v013-$Phase"
$dependencyManifest=if($Phase -cin @('3g-b','4a','4b-b')){'cmake/security/sec-maint-v013/3f'}else{$manifest}
$webpPhase=$Phase -cin @('3f','3g-b','4a','4b-b')
$plan=Read-S1StrictJson (Resolve-S1Path $root "$manifest/source-inputs-v1.json")
$contract=Resolve-S1Path $root 'cmake/security/sec-maint-v013/toolchain-v1.json'
$tools=Read-S1StrictJson $contract
# Resolve the approved tools before vcvarsall adds Visual Studio's bundled copies.
$cmake=(Get-Command cmake.exe -CommandType Application | Select-Object -First 1).Source
$ninja=(Get-Command ninja.exe -CommandType Application | Select-Object -First 1).Source
$ctest=Join-Path (Split-Path -Parent $cmake) ctest.exe
$cmakeVersion=@(& $cmake --version)
if(!$cmakeVersion.Count -or $cmakeVersion[0] -cne ('cmake version '+$tools.cmake)){Stop-S1 MaintCMake}
$ninjaVersion=& $ninja --version
if($ninjaVersion -cne $tools.ninja){Stop-S1 MaintNinja}
$ctestVersion=@(& $ctest --version)
if(!$ctestVersion.Count -or $ctestVersion[0] -cne ('ctest version '+$tools.cmake)){Stop-S1 MaintCTest}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $finder -latest -products Microsoft.VisualStudio.Product.BuildTools -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if($LASTEXITCODE -ne 0 -or !$vs){Stop-S1 MaintVisualStudio}
$batch=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$settings=& cmd.exe /d /c ('call "'+$batch+'" x64 '+$tools.windowsSDK+' -vcvars_ver='+$tools.toolsetDirectory+' >nul && set')
if($LASTEXITCODE -ne 0){Stop-S1 MaintDeveloperShell}
foreach($line in $settings){
    if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}
}
if($env:VCToolsVersion -cne $tools.toolsetDirectory -or $env:WindowsSDKVersion.TrimEnd('\') -cne $tools.windowsSDK -or
   $env:VSCMD_ARG_TGT_ARCH -cne 'x64'){Stop-S1 MaintToolchainSelection}
foreach($item in $tools.msvcFiles.GetEnumerator()){
    if((Get-FileHash -LiteralPath (Join-Path $env:VCToolsInstallDir $item.Key)).Hash -cne $item.Value){Stop-S1 MaintMsvcIdentity}
}
foreach($item in $tools.sdkFiles.GetEnumerator()){
    if((Get-FileHash -LiteralPath (Join-Path $env:WindowsSdkDir $item.Key)).Hash -cne $item.Value){Stop-S1 MaintSdkIdentity}
}
$cl=(Get-Command cl.exe -CommandType Application).Source
if((Get-Item -LiteralPath $cl).VersionInfo.FileVersion -cne $tools.compiler -or
   (Get-FileHash -LiteralPath $cl).Hash -cne $tools.msvcFiles['bin/Hostx64/x64/cl.exe']){Stop-S1 MaintCompiler}
$env:VCPKG_ROOT=Join-Path $run vcpkg
$env:VCPKG_DISABLE_METRICS='1'
$env:TEMP=Join-Path $run tmp
$env:TMP=$env:TEMP

# These fixed reference checkouts are read-only inputs. Do not modify Git configuration on disk.
$env:GIT_OPTIONAL_LOCKS='0'
$refs=@($ManagedOriginal) + @('third_party/highway','third_party/skcms','third_party/libjpeg-turbo' | ForEach-Object {Join-Path $ManagedOriginal $_})
$env:GIT_CONFIG_COUNT=[string]$refs.Count
for($i=0;$i -lt $refs.Count;$i++){
    [Environment]::SetEnvironmentVariable("GIT_CONFIG_KEY_$i",'safe.directory','Process')
    [Environment]::SetEnvironmentVariable("GIT_CONFIG_VALUE_$i",([IO.Path]::GetFullPath($refs[$i]).Replace('\','/')),'Process')
}
$null=Test-S1ManagedOriginal $root $ManagedOriginal
$null=Test-S1ManagedDerivative $root $ManagedDerivative
$null=& (Join-Path $PSScriptRoot validate_sec_maint_3a_install.ps1) -Mode Verify -RunRoot $run `
    -ReferenceInstalled $ReferenceInstalled -Phase $Phase -PreviousRun $PreviousRun -AvifReferenceRun $AvifReferenceRun

$reference=Read-S1StrictJson (Resolve-S1Path $root 'packaging/public/profile-icon-v012-rc1.json')
foreach($name in 'jxl.dll','jxl_cms.dll','jxl_threads.dll','hwy.dll','brotlidec.dll','brotlienc.dll','brotlicommon.dll','lcms2-2.dll'){
    $item=$reference.files | Where-Object path -CEQ $name
    Assert-S1Identity (Resolve-S1Path $ReferenceProduct $name) $item.size $item.sha256
}
$baselineProduct=$ReferenceProduct
$baselineIdentity=Resolve-S1Path $ReferenceProduct 'BUILD-IDENTITY.json'
if($Phase -cne '3a'){
    $baselineProduct=Resolve-S1Path $PreviousRun 'product'
    $baselineIdentity=Resolve-S1Path $PreviousRun 'BUILD-IDENTITY.json'
    $old=(Read-S1StrictJson $baselineIdentity).binaries
    foreach($item in $old){
        Assert-S1Identity (Resolve-S1Path $baselineProduct $item.path) $item.size $item.sha256
    }
}
if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b')){
    if(!$Png3aRun){Stop-S1 MaintPng3aReference}
    $reference3a=Resolve-S1Path $Png3aRun 'BUILD-IDENTITY.json'
    if((Get-FileHash -LiteralPath $reference3a).Hash -cne $plan.png3aBuildIdentitySha256){Stop-S1 MaintPng3aReference}
    foreach($item in (Read-S1StrictJson $reference3a).binaries | Where-Object path -CIn @('libpng16.dll','z.dll')){
        Assert-S1Identity (Resolve-S1Path $Png3aRun ('product/'+$item.path)) $item.size $item.sha256
    }
}
if($Phase -cin @('3d','3e','3f','3g-b','4a','4b-b')){
    if(!$Png3bRun){Stop-S1 MaintPng3bReference}
    $reference3b=Resolve-S1Path $Png3bRun 'BUILD-IDENTITY.json'
    if((Get-FileHash -LiteralPath $reference3b).Hash -cne $plan.png3bBuildIdentitySha256){Stop-S1 MaintPng3bReference}
    foreach($item in (Read-S1StrictJson $reference3b).binaries | Where-Object path -CIn @('libpng16.dll','z.dll')){
        Assert-S1Identity (Resolve-S1Path $Png3bRun ('product/'+$item.path)) $item.size $item.sha256
    }
}
$build=Join-Path $run build
$product=Join-Path $run product
if((Test-Path -LiteralPath $build) -or (Test-Path -LiteralPath $product)){Stop-S1 MaintBuildExists}
function Invoke-MaintTool([string]$Name,[string]$Exe,[string[]]$Arguments){
    $log=Join-Path $run ($Name+'.log')
    if(Test-Path -LiteralPath $log){Stop-S1 MaintLogExists}
    & $Exe @Arguments *> $log
    if($LASTEXITCODE -ne 0){Stop-S1 ('MaintToolFailed:'+ $Name)}
}

$cmakeOptions=[ordered]@{
    CMAKE_BUILD_TYPE='Release'
    CMAKE_MSVC_RUNTIME_LIBRARY='MultiThreadedDLL'
    CMAKE_EXPORT_COMPILE_COMMANDS='ON'
    CMAKE_MAKE_PROGRAM=$ninja
    CMAKE_TOOLCHAIN_FILE=(Join-Path $run 'vcpkg/scripts/buildsystems/vcpkg.cmake')
    CMAKE_FIND_USE_PACKAGE_REGISTRY='OFF'
    CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY='OFF'
    CLIPSS2_PUBLICATION_SCOPE=$scope
    CLIPSS2_SHIPPING_PRIVACY='ON'
    CLIPSS2_SHIPPING_PROFILE='v2'
    CLIPSS2_MAINT_RUN_ROOT=$run
    CLIPSS2_MAINT_REFERENCE_INSTALLED=$ReferenceInstalled
    CLIPSS2_MAINT_REFERENCE_PRODUCT=$ReferenceProduct
    CLIPSS2_JPEGLI_SOURCE_DIR=$ManagedOriginal
    CLIPSS2_DERIVED_JPEGLI_DIR=$ManagedDerivative
    VCPKG_MANIFEST_DIR=(Join-Path $root $dependencyManifest)
    VCPKG_INSTALLED_DIR=(Join-Path $run installed)
    VCPKG_MANIFEST_INSTALL='OFF'
    VCPKG_TARGET_TRIPLET=$tools.targetTriplet
    VCPKG_HOST_TRIPLET=$tools.hostTriplet
    VCPKG_OVERLAY_TRIPLETS=(Join-Path $root triplets)
    BUILD_TESTING='ON'
}
if($Phase -cne '3a'){
    $pngReference=if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b')){$Png3aRun}else{$PreviousRun}
    $cmakeOptions.CLIPSS2_MAINT_PNG_REFERENCE_INSTALLED=Resolve-S1Path $pngReference 'installed'
    $cmakeOptions.CLIPSS2_MAINT_PNG_REFERENCE_PRODUCT=Resolve-S1Path $pngReference 'product'
}
if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b')){
    $pngPrevious=if($Phase -cin @('3d','3e','3f','3g-b','4a','4b-b')){$Png3bRun}else{$PreviousRun}
    $cmakeOptions.CLIPSS2_MAINT_PNG_PREVIOUS_INSTALLED=Resolve-S1Path $pngPrevious 'installed'
    $cmakeOptions.CLIPSS2_MAINT_PNG_PREVIOUS_PRODUCT=Resolve-S1Path $pngPrevious 'product'
    $cmakeOptions.CLIPSS2_MAINT_PNG_PREVIOUS_RUN=[IO.Path]::GetFullPath($pngPrevious)
    $cmakeOptions.CLIPSS2_MAINT_PREVIOUS_RUN=[IO.Path]::GetFullPath($PreviousRun)
}
if($Phase -cin @('3e','3f','3g-b','4a','4b-b')){
    if(!$TurboReferenceRun -or
       (Get-FileHash -LiteralPath (Resolve-S1Path $TurboReferenceRun 'BUILD-IDENTITY.json')).Hash -cne
           $plan.turboReferenceBuildIdentitySha256){Stop-S1 MaintTurboReference}
    $cmakeOptions.CLIPSS2_MAINT_TURBO_REFERENCE_RUN=[IO.Path]::GetFullPath($TurboReferenceRun)
}
if($webpPhase){
    if(!$AvifReferenceRun -or (Get-FileHash -LiteralPath (Resolve-S1Path $AvifReferenceRun 'BUILD-IDENTITY.json')).Hash -cne
       $plan.avifReferenceBuildIdentitySha256){Stop-S1 MaintAvifReference}
    $cmakeOptions.CLIPSS2_MAINT_AVIF_REFERENCE_RUN=[IO.Path]::GetFullPath($AvifReferenceRun)
    $cmakeOptions.CLIPSS2_MAINT_WEBP_SOURCE=Resolve-S1Path $run $plan.libwebp.probeSource
}
if($Phase -cin @('3g-b','4a','4b-b')){
    if(!$WebpReferenceRun -or (Get-FileHash -LiteralPath (Resolve-S1Path $WebpReferenceRun 'BUILD-IDENTITY.json')).Hash -cne
       $plan.webpReferenceBuildIdentitySha256){Stop-S1 MaintWebpReference}
    $cmakeOptions.CLIPSS2_MAINT_WEBP_REFERENCE_RUN=[IO.Path]::GetFullPath($WebpReferenceRun)
}
$arguments=@('-S',$root,'-B',$build,'-G','Ninja')+@($cmakeOptions.GetEnumerator() | ForEach-Object {
    '-D'+$_.Key+'='+$_.Value.Replace('\','/')
})
Invoke-MaintTool configure $cmake $arguments
Invoke-MaintTool build $cmake @('--build',$build,'--parallel',[string]$Jobs)
if($Phase -cin @('3g-b','4a','4b-b')){
    # Keep tests that resolve the executable's default store inside this run.
    [IO.File]::Copy((Resolve-S1Path $root 'packaging/clipss2.portable'),(Join-Path $build 'clipss2.portable'),$false)
}
Invoke-MaintTool ctest $ctest @('--test-dir',$build,'--output-on-failure','--output-junit',(Join-Path $run 'ctest.xml'))

$profilePath=Resolve-S1Path $root "packaging/public/profile-$scope.json"
$profile=Read-S1StrictJson $profilePath
New-S1OwnedDirectory $product
$installed=Join-Path $run installed
$bin=Join-Path $installed ($tools.targetTriplet+'/bin')
$installedShare=Join-Path $installed ($tools.targetTriplet+'/share')
$licenses=@{ 'libjxl/copyright'='licenses/libjxl/copyright'; 'libjxl/PATENTS'='licenses/libjxl/PATENTS'; 'brotli/copyright'='licenses/brotli/copyright' }
if($Phase -cne '3a'){$licenses['libpng/copyright']='licenses/libpng/copyright'}
if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b')){$licenses['zlib/copyright']=$plan.zlib.license}
if($Phase -cin @('3d','3e','3f','3g-b','4a','4b-b')){$licenses['libjpeg-turbo/copyright']='licenses/libjpeg-turbo/copyright'}
if($Phase -cin @('3e','3f','3g-b','4a','4b-b')){
    $licenses['aom/copyright']=$plan.aom.license
    $licenses['libyuv/copyright']=$plan.libyuv.license
}
if($webpPhase){$licenses['libwebp/copyright']=$plan.libwebp.license}
foreach($binding in $licenses.GetEnumerator()){
    if((Get-FileHash -LiteralPath (Resolve-S1Path $installedShare $binding.Key)).Hash -cne
        (Get-FileHash -LiteralPath (Resolve-S1Path $root $binding.Value)).Hash){Stop-S1 MaintLicense}
}
if($Phase -cin @('3d','3e','3f','3g-b','4a','4b-b')){
    $spngCopyright=[IO.File]::ReadAllText((Resolve-S1Path $installedShare 'libspng/copyright')).Replace("`r`n","`n")
    $spngLicense=[IO.File]::ReadAllText((Resolve-S1Path $root $plan.libspng.license)).Replace("`r`n","`n").TrimEnd()
    if(!$spngCopyright.Contains($spngLicense)){Stop-S1 MaintSpngLicense}
    $rawNotices=[IO.File]::ReadAllText((Resolve-S1Path $root $plan.libspng.libpngNotices))
    foreach($notice in [regex]::Matches($rawNotices,'/\*[\s\S]*?\*/')){
        if(!$spngCopyright.Contains($notice.Value)){Stop-S1 MaintSpngLicense}
    }
}
$binaries=@(foreach($name in @('clipss2.exe')+@($profile.productRuntimeDlls)){
    $from=Resolve-S1Path $build $name
    if($name -cne 'clipss2.exe' -and (Get-FileHash -LiteralPath $from).Hash -cne
       (Get-FileHash -LiteralPath (Resolve-S1Path $bin $name)).Hash){Stop-S1 MaintAppLocalIdentity}
    if($Phase -ceq '3b' -and $name -cnotin @('clipss2.exe','libpng16.dll')){
        if((Get-FileHash -LiteralPath $from).Hash -cne
           (Get-FileHash -LiteralPath (Resolve-S1Path $baselineProduct $name)).Hash){Stop-S1 MaintFixedDllIdentity}
    }
    if(($Phase -ceq '3d' -and $name -cin @('libz.dll','libpng16.dll','jxl.dll','brotlidec.dll')) -or
       ($Phase -ceq '3e' -and $name -cnotin @('clipss2.exe','aom.dll','libyuv.dll','avif.dll')) -or
       ($Phase -ceq '3f' -and $name -cnotin @('clipss2.exe','libwebp.dll','libsharpyuv.dll')) -or
       ($Phase -cin @('3g-b','4a','4b-b') -and $name -cne 'clipss2.exe')){
        if((Get-FileHash -LiteralPath $from).Hash -cne
           (Get-FileHash -LiteralPath (Resolve-S1Path $baselineProduct $name)).Hash){Stop-S1 Maint3cDllDrift}
    }
    $destination=Join-Path $product $name
    [IO.File]::Copy($from,$destination,$false)
    Read-IconPE $destination
})
$allowed=$profile.productRuntimeDlls
$mustImport=[ordered]@{
    'clipss2.exe'=@('jxl.dll','jxl_threads.dll')
    'jxl.dll'=@('jxl_cms.dll','hwy.dll','brotlidec.dll','brotlienc.dll')
    'jxl_cms.dll'=@('lcms2-2.dll','hwy.dll')
}
if($Phase -cne '3a'){
    $mustImport['clipss2.exe']+=@('libpng16.dll')
    $mustImport['libpng16.dll']=if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b')){@('libz.dll')}else{@('z.dll')}
}
if($Phase -cin @('3d','3e','3f','3g-b','4a','4b-b')){
    $mustImport['clipss2.exe']+=@('turbojpeg.dll')
    $mustImport['turbojpeg.dll']=@('spng.dll')
    $mustImport['spng.dll']=@('libz.dll')
    $mustImport['libyuv.dll']=@('jpeg62.dll')
    $mustImport['avif.dll']=@('libyuv.dll')
}
if($Phase -cin @('3e','3f','3g-b','4a','4b-b')){
    $mustImport['clipss2.exe']+=@('avif.dll')
    $mustImport['avif.dll']+=@('aom.dll')
}
if($webpPhase){
    $mustImport['clipss2.exe']+=@('libwebp.dll')
    $mustImport['libwebp.dll']=@('libsharpyuv.dll')
}
foreach($check in $mustImport.GetEnumerator()){
    $imports=($binaries | Where-Object path -CEQ $check.Key).imports
    foreach($dll in $check.Value){if($dll -notin $imports){Stop-S1 ('MaintMissingImport:'+ $dll)}}
}
# Preserve the external DLL families of the previous accepted product.
$external=@((Read-S1StrictJson $baselineIdentity).binaries.imports |
    Where-Object {$_ -notin $allowed} | Sort-Object -Unique)
if(!$external.Count){
    $external=@(foreach($name in @('clipss2.exe')+@($allowed)){
        (Read-IconPE (Resolve-S1Path $baselineProduct $name)).imports | Where-Object {$_ -notin $allowed}
    }) | Sort-Object -Unique
}
$settingsSystemDlls=@('api-ms-win-security-base-l1-1-0.dll',
    'api-ms-win-security-provider-l1-1-0.dll','bcrypt.dll')
foreach($binary in $binaries){
    $approvedExternal=$external
    if($Phase -ceq '4b-b' -and $binary.path -ceq 'clipss2.exe'){$approvedExternal+=$settingsSystemDlls}
    if(@($binary.imports | Where-Object {$_ -notin $allowed -and $_ -notin $approvedExternal}).Count){Stop-S1 MaintUnexpectedImport}
    if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b') -and 'z.dll' -in $binary.imports){Stop-S1 MaintLegacyZlibImport}
}
$dumpbin=(Get-Command dumpbin.exe -CommandType Application).Source
if($Phase -ceq '4b-b'){
    function Get-MaintImportSymbols($Path){
        $dll=''
        $lines=@(& $dumpbin /imports $Path)
        if($LASTEXITCODE -ne 0){Stop-S1 MaintSettingsImportInspection}
        foreach($line in $lines){
            if($line -match '^\s{4}(\S+\.dll)\s*$'){$dll=$Matches[1].ToLowerInvariant()}
            elseif($dll -and $line -cmatch '^\s{12,}(?:[0-9A-Fa-f]+\s+(\S+)|Ordinal\s+(\d+))\s*$'){
                $symbol=if($Matches[1]){$Matches[1]}else{'ordinal:'+$Matches[2]}
                "$dll/$symbol"
            }
        }
    }
    $before=@(Get-MaintImportSymbols (Resolve-S1Path $baselineProduct 'clipss2.exe'))
    $after=@(Get-MaintImportSymbols (Resolve-S1Path $product 'clipss2.exe'))
    $new=@($after | Where-Object {$_ -cnotin $before})
    $old=@($before | Where-Object {$_ -cnotin $after})
    $expected=@(
        'api-ms-win-core-handle-l1-1-0.dll/GetHandleInformation',
        'api-ms-win-core-processthreads-l1-1-0.dll/OpenProcessToken',
        'api-ms-win-core-synch-l1-1-0.dll/CreateMutexExW',
        'api-ms-win-core-synch-l1-1-0.dll/ReleaseMutex',
        'api-ms-win-security-provider-l1-1-0.dll/GetSecurityInfo',
        'bcrypt.dll/BCryptHash'
    )+@('AddAccessAllowedAce','EqualSid','GetAce','GetSecurityDescriptorDacl','GetSidLengthRequired',
        'GetTokenInformation','InitializeAcl','InitializeSecurityDescriptor','IsValidSid',
        'SetSecurityDescriptorDacl','SetSecurityDescriptorOwner' | ForEach-Object {"api-ms-win-security-base-l1-1-0.dll/$_"})
    if($old.Count -or (($new | Sort-Object -CaseSensitive) -join ',') -cne
        (($expected | Sort-Object -CaseSensitive) -join ',')){Stop-S1 MaintSettingsImportDelta}
}
foreach($binary in $binaries){
    $headers=@(& $dumpbin /headers (Resolve-S1Path $product $binary.path))
    if($LASTEXITCODE -ne 0){Stop-S1 MaintPEHeaders}
    $delay=@($headers | Where-Object {$_ -match 'RVA \[size\] of Delay Import Directory'})
    if($delay.Count -ne 1 -or $delay[0] -notmatch '^\s*0+\s+\[\s*0+\]'){Stop-S1 MaintDelayImports}
    $binary.delayImports=@()
    $binary.codeView=@(foreach($line in $headers){
        if($line -match 'Format:\s*RSDS,\s*[^,]+,\s*\d+,\s*(.+)$'){
            $pdb=$Matches[1].Trim()
            if($pdb -notmatch '^[A-Za-z0-9_.-]+\.pdb$'){Stop-S1 MaintCodeViewPath}
            $pdb
        }
    })
}
function Get-MaintExports([string]$Path,[string]$Prefix){
    $lines=@(& $dumpbin /exports $Path)
    if($LASTEXITCODE -ne 0){Stop-S1 MaintPEExports}
    foreach($line in $lines){
        if($line -cmatch ('^\s*\d+\s+[A-Fa-f0-9]+\s+[A-Fa-f0-9]+\s+('+$Prefix+'\w+)\b')){$Matches[1]}
    }
}
foreach($name in 'jxl.dll','jxl_cms.dll','jxl_threads.dll'){
    $before=@(Get-MaintExports (Resolve-S1Path $baselineProduct $name) Jxl)
    $after=@(Get-MaintExports (Resolve-S1Path $product $name) Jxl)
    if(!$before.Count -or !$after.Count -or @($before | Where-Object {$_ -cnotin $after}).Count){Stop-S1 MaintMissingJxlExport}
    ($binaries | Where-Object path -CEQ $name).jxlExports=$after
}
if($Phase -cne '3a'){
    $before=@(Get-MaintExports (Resolve-S1Path $baselineProduct 'libpng16.dll') png_)
    $after=@(Get-MaintExports (Resolve-S1Path $product 'libpng16.dll') png_)
    if(!$before.Count -or !$after.Count -or @($before | Where-Object {$_ -cnotin $after}).Count){Stop-S1 MaintMissingPngExport}
    ($binaries | Where-Object path -CEQ 'libpng16.dll').pngExports=$after
}
if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b')){
    $priorZlib=if($Phase -cin @('3d','3e','3f','3g-b','4a','4b-b')){'libz.dll'}else{'z.dll'}
    $legacyExports=@(Get-MaintExports (Resolve-S1Path $baselineProduct $priorZlib) '')
    $newExports=@(Get-MaintExports (Resolve-S1Path $product 'libz.dll') '')
    $importLines=@(& $dumpbin /imports:libz.dll (Resolve-S1Path $product 'libpng16.dll'))
    if($LASTEXITCODE -ne 0){Stop-S1 MaintZlibImports}
    $used=@($importLines|ForEach-Object {if($_ -cmatch '^\s+[A-F0-9]+\s+([A-Za-z_]\w+)\s*$'){$Matches[1]}})
    if(!$legacyExports.Count -or !$newExports.Count -or !$used.Count -or
       @($used|Where-Object {$_ -cnotin $newExports}).Count){Stop-S1 MaintZlibExports}
    ($binaries | Where-Object path -CEQ 'libz.dll').exportChanges=[ordered]@{
        priorCount=$legacyExports.Count;count=$newExports.Count;pngImports=$used;
        removed=@($legacyExports|Where-Object {$_ -cnotin $newExports});
        added=@($newExports|Where-Object {$_ -cnotin $legacyExports})}
}
if($Phase -cin @('3d','3e','3f','3g-b','4a','4b-b')){
    foreach($dll in 'turbojpeg.dll','jpeg62.dll'){
        $before=@(Get-MaintExports (Resolve-S1Path $baselineProduct $dll) '')
        $after=@(Get-MaintExports (Resolve-S1Path $product $dll) '')
        $removed=@($before|Where-Object {$_ -cnotin $after})
        if(!$before.Count -or $removed.Count){Stop-S1 MaintJpegExport}
        ($binaries|Where-Object path -CEQ $dll).exportChanges=[ordered]@{priorCount=$before.Count;
            count=$after.Count;added=@($after|Where-Object {$_ -cnotin $before});removed=$removed}
    }
    $spng=@(Get-MaintExports (Resolve-S1Path $product 'spng.dll') 'spng_')
    if(!$spng.Count -or 'spng_version_string' -cnotin $spng){Stop-S1 MaintSpngExports}
    ($binaries|Where-Object path -CEQ 'spng.dll').spngExports=$spng
}
if($Phase -cin @('3e','3f','3g-b','4a','4b-b')){
    foreach($bind in @(
        @{consumer='clipss2.exe';dll='avif.dll'},
        @{consumer='avif.dll';dll='aom.dll'},
        @{consumer='avif.dll';dll='libyuv.dll'},
        @{consumer='libyuv.dll';dll='jpeg62.dll'}
    )){
        $before=@(Get-MaintExports (Resolve-S1Path $baselineProduct $bind.dll) '')
        $after=@(Get-MaintExports (Resolve-S1Path $product $bind.dll) '')
        $importLines=@(& $dumpbin ("/imports:"+$bind.dll) (Resolve-S1Path $product $bind.consumer))
        if($LASTEXITCODE -ne 0){Stop-S1 MaintAvifImportInspection}
        $used=@($importLines|ForEach-Object {if($_ -cmatch '^\s+[A-F0-9]+\s+([A-Za-z_]\w+)\s*$'){$Matches[1]}})
        $usedOrdinals=@($importLines|ForEach-Object {if($_ -match '^\s+Ordinal\s+(\d+)\s*$'){[int]$Matches[1]}})
        $beforeOrdinals=@(& $dumpbin /exports (Resolve-S1Path $baselineProduct $bind.dll) | ForEach-Object {
            if($_ -match '^\s*(\d+)\s+[A-F0-9]+\s+[A-F0-9]+\s+(.+)$'){
                [ordered]@{ordinal=[int]$Matches[1];name=$Matches[2].Trim()}
            }
        })
        if($LASTEXITCODE -ne 0){Stop-S1 MaintAvifExportInspection}
        $afterOrdinals=@(& $dumpbin /exports (Resolve-S1Path $product $bind.dll) | ForEach-Object {
            if($_ -match '^\s*(\d+)\s+[A-F0-9]+\s+[A-F0-9]+\s+(.+)$'){
                [ordered]@{ordinal=[int]$Matches[1];name=$Matches[2].Trim()}
            }
        })
        if($LASTEXITCODE -ne 0){Stop-S1 MaintAvifExportInspection}
        if(!$before.Count -or !$after.Count -or !($used.Count+$usedOrdinals.Count) -or
           @($used|Where-Object {$_ -cnotin $after}).Count){
            Stop-S1 MaintAvifRequiredExports
        }
        $ordinalImports=@(foreach($ordinal in $usedOrdinals){
            $prior=@($beforeOrdinals|Where-Object ordinal -EQ $ordinal)
            $current=@($afterOrdinals|Where-Object ordinal -EQ $ordinal)
            if($prior.Count -ne 1 -or $current.Count -ne 1 -or $prior[0].name -cne $current[0].name){
                Stop-S1 MaintAvifRequiredOrdinals
            }
            $current[0]
        })
        ($binaries | Where-Object path -CEQ $bind.dll).avifExportChanges=[ordered]@{
            consumer=$bind.consumer;priorCount=$before.Count;count=$after.Count;imports=$used;
            ordinalImports=$ordinalImports;exports=$afterOrdinals;
            removed=@($before|Where-Object {$_ -cnotin $after});
            added=@($after|Where-Object {$_ -cnotin $before})}
    }
}
if($webpPhase){
    foreach($dll in 'libwebp.dll','libsharpyuv.dll'){
        $exports=@(foreach($directory in $baselineProduct,$product){
            $lines=@(& $dumpbin /exports (Resolve-S1Path $directory $dll))
            if($LASTEXITCODE -ne 0){Stop-S1 MaintWebpExports}
            ,@($lines|ForEach-Object {
                if($_ -cmatch '^\s*(\d+)\s+[A-Fa-f0-9]+\s+[A-Fa-f0-9]+\s+(\w+)\b'){
                    "$($Matches[1]):$($Matches[2])"
                }
            })
        })
        if(!$exports[0].Count -or ($exports[0] -join ',') -cne ($exports[1] -join ',')){Stop-S1 MaintWebpExports}
        ($binaries|Where-Object path -CEQ $dll).webpExports=$exports[1]
    }
}
$marker=Join-Path $installed "share/clipss2-sec-maint-$Phase-installed-v1.json"
$receipt=Join-Path $installed "share/clipss2-sec-maint-$Phase-install-receipt-v1.json"
$head=& git --no-optional-locks -C $root rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $head -cne $plan.baseCommit){Stop-S1 MaintBuildSource}
$identity=[ordered]@{schemaVersion=1;version='0.1.3';scope=$scope;sourceBaseCommit=$head;sourceBindings=$plan.files;
    configuration='MSVC x64 Release /MD';compiler=$tools.compiler;windowsSDK=$tools.windowsSDK;
    toolchainSha256=(Get-FileHash -LiteralPath $contract).Hash;
    sourceInputsSha256=(Get-FileHash -LiteralPath (Resolve-S1Path $root "$manifest/source-inputs-v1.json")).Hash;
    installedReceiptSha256=(Get-FileHash -LiteralPath $receipt).Hash;installedMarkerSha256=(Get-FileHash -LiteralPath $marker).Hash;
    binaries=$binaries;runtimeBundled=$false;releaseApproved=$false;ownerAcceptance='PENDING'}
if($Phase -cne '3a'){$identity.previousBuildIdentitySha256=$plan.referenceBuildIdentitySha256}
if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b')){
    $identity.profileSha256=(Get-FileHash -LiteralPath $profilePath).Hash
    $identity.productRuntimeDlls=$profile.productRuntimeDlls
    $identity.installedRuntimeDlls=$plan.installedRuntimeDlls
    $identity.zlib=$plan.zlib
    $identity.embedded=$plan.embedded
}
if($Phase -cin @('3d','3e','3f','3g-b','4a','4b-b')){
    $identity.libjpegTurbo=$plan.libjpegTurbo
    $identity.libspng=$plan.libspng
    $identity.externalLinkEvidenceSha256=$plan.externalLinkEvidenceSha256
}
if($Phase -cin @('3e','3f','3g-b','4a','4b-b')){
    $identity.aom=$plan.aom
    $identity.libyuv=$plan.libyuv
}
if($webpPhase){$identity.libwebp=$plan.libwebp}
Write-S1NewJson (Join-Path $run 'BUILD-IDENTITY.json') $identity
if($Phase -cin @('3c','3d','3e','3f','3g-b','4a','4b-b')){
    & (Join-Path $PSScriptRoot test_sec_maint_3c_package.ps1) -Mode Verify -RunRoot $run `
        -ProductDirectory $product -PreviousRun $PreviousRun -Phase $Phase
}
[ordered]@{result='PASS';binaries=$binaries.Count;releaseApproved=$false}|ConvertTo-Json
