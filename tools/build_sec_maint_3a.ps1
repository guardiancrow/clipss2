#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][string]$ReferenceInstalled,
    [Parameter(Mandatory)][string]$ReferenceProduct,
    [Parameter(Mandatory)][string]$ManagedOriginal,
    [Parameter(Mandatory)][string]$ManagedDerivative,
    [ValidateSet('3a','3b','3c')][string]$Phase='3a',
    [string]$PreviousRun,
    [string]$Png3aRun,
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
    -ReferenceInstalled $ReferenceInstalled -Phase $Phase -PreviousRun $PreviousRun

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
if($Phase -ceq '3c'){
    if(!$Png3aRun){Stop-S1 MaintPng3aReference}
    $reference3a=Resolve-S1Path $Png3aRun 'BUILD-IDENTITY.json'
    if((Get-FileHash -LiteralPath $reference3a).Hash -cne $plan.png3aBuildIdentitySha256){Stop-S1 MaintPng3aReference}
    foreach($item in (Read-S1StrictJson $reference3a).binaries | Where-Object path -CIn @('libpng16.dll','z.dll')){
        Assert-S1Identity (Resolve-S1Path $Png3aRun ('product/'+$item.path)) $item.size $item.sha256
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
    VCPKG_MANIFEST_DIR=(Join-Path $root $manifest)
    VCPKG_INSTALLED_DIR=(Join-Path $run installed)
    VCPKG_MANIFEST_INSTALL='OFF'
    VCPKG_TARGET_TRIPLET=$tools.targetTriplet
    VCPKG_HOST_TRIPLET=$tools.hostTriplet
    VCPKG_OVERLAY_TRIPLETS=(Join-Path $root triplets)
    BUILD_TESTING='ON'
}
if($Phase -cne '3a'){
    $pngReference=if($Phase -ceq '3c'){$Png3aRun}else{$PreviousRun}
    $cmakeOptions.CLIPSS2_MAINT_PNG_REFERENCE_INSTALLED=Resolve-S1Path $pngReference 'installed'
    $cmakeOptions.CLIPSS2_MAINT_PNG_REFERENCE_PRODUCT=Resolve-S1Path $pngReference 'product'
}
if($Phase -ceq '3c'){
    $cmakeOptions.CLIPSS2_MAINT_PNG_PREVIOUS_INSTALLED=Resolve-S1Path $PreviousRun 'installed'
    $cmakeOptions.CLIPSS2_MAINT_PNG_PREVIOUS_PRODUCT=$baselineProduct
    $cmakeOptions.CLIPSS2_MAINT_PREVIOUS_RUN=[IO.Path]::GetFullPath($PreviousRun)
}
$arguments=@('-S',$root,'-B',$build,'-G','Ninja')+@($cmakeOptions.GetEnumerator() | ForEach-Object {
    '-D'+$_.Key+'='+$_.Value.Replace('\','/')
})
Invoke-MaintTool configure $cmake $arguments
Invoke-MaintTool build $cmake @('--build',$build,'--parallel',[string]$Jobs)
Invoke-MaintTool ctest $ctest @('--test-dir',$build,'--output-on-failure','--output-junit',(Join-Path $run 'ctest.xml'))

$profilePath=Resolve-S1Path $root "packaging/public/profile-$scope.json"
$profile=Read-S1StrictJson $profilePath
New-S1OwnedDirectory $product
$installed=Join-Path $run installed
$bin=Join-Path $installed ($tools.targetTriplet+'/bin')
$installedShare=Join-Path $installed ($tools.targetTriplet+'/share')
$licenses=@{ 'libjxl/copyright'='licenses/libjxl/copyright'; 'libjxl/PATENTS'='licenses/libjxl/PATENTS'; 'brotli/copyright'='licenses/brotli/copyright' }
if($Phase -cne '3a'){$licenses['libpng/copyright']='licenses/libpng/copyright'}
if($Phase -ceq '3c'){$licenses['zlib/copyright']=$plan.zlib.license}
foreach($binding in $licenses.GetEnumerator()){
    if((Get-FileHash -LiteralPath (Resolve-S1Path $installedShare $binding.Key)).Hash -cne
        (Get-FileHash -LiteralPath (Resolve-S1Path $root $binding.Value)).Hash){Stop-S1 MaintLicense}
}
$binaries=@(foreach($name in @('clipss2.exe')+@($profile.productRuntimeDlls)){
    $from=Resolve-S1Path $build $name
    if($name -cne 'clipss2.exe' -and (Get-FileHash -LiteralPath $from).Hash -cne
       (Get-FileHash -LiteralPath (Resolve-S1Path $bin $name)).Hash){Stop-S1 MaintAppLocalIdentity}
    if($Phase -ceq '3b' -and $name -cnotin @('clipss2.exe','libpng16.dll')){
        if((Get-FileHash -LiteralPath $from).Hash -cne
           (Get-FileHash -LiteralPath (Resolve-S1Path $baselineProduct $name)).Hash){Stop-S1 MaintFixedDllIdentity}
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
    $mustImport['libpng16.dll']=if($Phase -ceq '3c'){@('libz.dll')}else{@('z.dll')}
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
foreach($binary in $binaries){
    if(@($binary.imports | Where-Object {$_ -notin $allowed -and $_ -notin $external}).Count){Stop-S1 MaintUnexpectedImport}
    if($Phase -ceq '3c' -and 'z.dll' -in $binary.imports){Stop-S1 MaintLegacyZlibImport}
}
$dumpbin=(Get-Command dumpbin.exe -CommandType Application).Source
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
if($Phase -ceq '3c'){
    $legacyExports=@(Get-MaintExports (Resolve-S1Path $baselineProduct 'z.dll') '')
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
if($Phase -ceq '3c'){
    $identity.profileSha256=(Get-FileHash -LiteralPath $profilePath).Hash
    $identity.productRuntimeDlls=$profile.productRuntimeDlls
    $identity.installedRuntimeDlls=$plan.installedRuntimeDlls
    $identity.zlib=$plan.zlib
    $identity.embedded=$plan.embedded
}
Write-S1NewJson (Join-Path $run 'BUILD-IDENTITY.json') $identity
if($Phase -ceq '3c'){
    & (Join-Path $PSScriptRoot test_sec_maint_3c_package.ps1) -Mode Verify -RunRoot $run `
        -ProductDirectory $product -PreviousRun $PreviousRun
}
[ordered]@{result='PASS';binaries=$binaries.Count;releaseApproved=$false}|ConvertTo-Json
