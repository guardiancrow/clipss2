#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
. (Join-Path $PSScriptRoot ShippingProfileSecBR1.ps1)
$script:BR2InputsSha='5CC40FDA714E820DDFD6A14527E68FA3A2A448E60AB7748EEEFE6A6202396560'
function Read-BR2Ledger($Root) {
    $file=Resolve-S1Path $Root 'cmake/security/sec-br2/build-inputs-v1.json'
    if((Get-S1Hash ([IO.File]::ReadAllBytes($file))) -cne $script:BR2InputsSha){Stop-S1 BR2Ledger}
    $ledger=Read-S1StrictJson $file
    foreach($item in $ledger.files){Assert-S1Identity (Resolve-S1Path $Root $item.path) $item.size $item.sha256}
    return $ledger
}
function Initialize-BR2ContractView($Root) {
    $ledger=Read-BR2Ledger $Root
    $view=Resolve-S1Path $Root $ledger.br1ContractView
    if(Test-Path -LiteralPath $view){Stop-S1 BR2ContractViewExists}
    $s1=Read-S1SourceInputs $Root
    $br1File=Resolve-S1Path $Root 'cmake/security/sec-br1/source-inputs-v1.json'
    if((Get-S1Hash ([IO.File]::ReadAllBytes($br1File))) -cne $script:BR1LedgerSha){Stop-S1 BR1LedgerIdentity}
    $br1=Read-S1StrictJson $br1File
    $files=@($s1.inputs.files.path)+@($br1.files.path)+@(
        'cmake/privacy/source-inputs-s1-v1.json','cmake/security/sec-br1/source-inputs-v1.json',
        'tools/ShippingProfileSecBR1.ps1','tools/build_shipping_dependencies_sec_br1.ps1',
        'tools/build_shipping_dependencies_s1.ps1',
        'tests/sec_br1_contract_test.ps1','tests/s1_dependency_install_test.ps1')
    New-S1OwnedDirectory $view
    foreach($relative in ($files|Select-Object -Unique)){
        $source=if($relative -cin @('CMakeLists.txt','cmake/ShippingPrivacy.cmake')){
            Resolve-S1Path $Root ('cmake/security/sec-br2/br1-preimages/'+$relative)
        }else{Resolve-S1Path $Root $relative}
        $dest=Resolve-S1Path $view $relative
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        [IO.File]::Copy($source,$dest,$false)
    }
    $null=Read-BR1SourceInputs $view
    return [ordered]@{result='PASS';scope='historical BR1 contract and tests only';view=$ledger.br1ContractView}
}
function Read-BR2SourceInputs($Root) {
    $inputs=Read-BR2Ledger $Root
    $null=Read-S1SourceInputs $Root
    $br1=Read-BR1SourceInputs (Resolve-S1Path $Root $inputs.br1ContractView)
    Assert-S1Value $br1.sourceInputsSha256 $inputs.dependencySourceInputsSha256 BR2DependencyContract
    foreach($item in $br1.inputs.files){
        if($item.path -cin @('CMakeLists.txt','cmake/ShippingPrivacy.cmake')){continue}
        Assert-S1Identity (Resolve-S1Path $Root $item.path) $item.size $item.sha256
    }
    $overlay=$br1.inputs.overlayRoots[0]
    $files=Get-S1Files (Resolve-S1Path $Root $overlay)
    $wanted=@($br1.inputs.files.path|Where-Object {$_.StartsWith($overlay+'/',[StringComparison]::Ordinal)}|ForEach-Object {$_.Substring($overlay.Length+1)})
    Assert-S1InputSet $wanted @($files.Keys)
    Assert-S1DirectorySet (Resolve-S1Path $Root $overlay) @($files.Keys)
    return [pscustomobject]@{inputs=$inputs;br1=$br1;sha256=$script:BR2InputsSha}
}
function Test-BR2Dependencies($Root,$Plan) {
    foreach($record in $Plan.inputs.dependencyEvidence){Assert-S1Identity (Resolve-S1Path $Root $record.path) $record.size $record.sha256}
    $null=Test-BR1Installed $Root $Plan.br1
    $paths=Get-BR1Paths $Root $Plan.br1
    Assert-S1Checkout $paths.vcpkg $Plan.br1.inputs.builtinBaseline
    $receipt=Read-S1StrictJson (Resolve-S1Path $paths.installed $script:BR1Receipt)
    if((Get-S1Hash ([IO.File]::ReadAllBytes((Join-Path $paths.vcpkg vcpkg.exe)))) -cne $receipt.vcpkgToolSha256){Stop-S1 BR2ToolChanged}
    return [ordered]@{result='PASS';brotli='1.2.1';libjxl='0.12.0';packages=16;installedDlls=19;dependencyContract='SEC-BR1';dependencyFilesWritten=$false;releaseApproved=$false}
}
function Get-BR2Paths($Root,$Plan) {
    $paths=Get-BR1Paths $Root $Plan.br1
    $paths.product=Resolve-S1Path $Root $Plan.inputs.productRoot
    $paths.dependencies=Resolve-S1Path $Root 'build/sec-br2/execution'
    return $paths
}
function Get-BR2Git($Root,$Arguments) {
    return & (Get-Module S1SourceContracts) {param($r,$a) Invoke-S1Git $r $a} $Root $Arguments
}
function Invoke-BR2Configure($Root,$Plan) {
    $null=Test-BR2Dependencies $Root $Plan
    $paths=Get-BR2Paths $Root $Plan
    if(Test-Path -LiteralPath $paths.product){Stop-S1 BR2ProductExists}
    if(Test-Path -LiteralPath $paths.dependencies){Stop-S1 BR2ExecutionExists}
    if((Get-BR2Git $Root @('status','--porcelain=v1','--untracked-files=all'))){Stop-S1 BR2SourceDirty}
    New-S1OwnedDirectory $paths.dependencies
    foreach($dir in @('logs','tmp','downloads','registries')){New-S1OwnedDirectory (Join-Path $paths.dependencies $dir)}
    $r=$Root.Replace('\','/')
    $cmakeArguments=@('-S',$r,'-B',$paths.product,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        '-DCLIPSS2_SHIPPING_PRIVACY=ON','-DCLIPSS2_SHIPPING_PROFILE=v2','-DCLIPSS2_PUBLICATION_SCOPE=sec-br2',
        '-DVCPKG_MANIFEST_INSTALL=OFF','-DVCPKG_TARGET_TRIPLET=x64-windows-shipping-privacy','-DVCPKG_HOST_TRIPLET=x64-windows',
        '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF','-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
        '-DCLIPSS2_BUILD_JPEG_BENCH=OFF','-DCLIPSS2_BUILD_LOSSY_EVAL=OFF',
        ('-DCMAKE_TOOLCHAIN_FILE='+$r+'/build/sec-br1-r1/vcpkg/scripts/buildsystems/vcpkg.cmake'),
        ('-DVCPKG_MANIFEST_DIR='+$r+'/cmake/security/sec-br1'),
        ('-DVCPKG_OVERLAY_PORTS='+$r+'/vcpkg-overlays/security-sec-br1;'+$r+'/vcpkg-overlays/shipping-privacy-v2'),
        ('-DVCPKG_OVERLAY_TRIPLETS='+$r+'/triplets'),('-DVCPKG_INSTALLED_DIR='+$r+'/build/sec-br1-r1/installed'),
        ('-DCLIPSS2_JPEGLI_SOURCE_DIR='+$r+'/build/managed-jpegli-src'),('-DCLIPSS2_DERIVED_JPEGLI_DIR='+$r+'/build/sec-br1-r1/managed-jpegli'))
    $cmake=(Get-Command cmake -CommandType Application|Select-Object -First 1).Source
    $null=Invoke-S1InstallCommand configure $cmake $cmakeArguments $Root $paths
    Write-S1NewJson (Resolve-S1Path $Root 'build/sec-br2/configure-identity.json') ([ordered]@{
        schemaVersion=1;sourceCommit=(Get-BR2Git $Root @('rev-parse','HEAD'));sourceTree=(Get-BR2Git $Root @('rev-parse','HEAD^{tree}'));
        inputSha256=$Plan.sha256;version='0.1.1';configuration='MSVC x64 Release /MD';releaseApproved=$false})
}
function Test-BR2Product($Root,$Plan) {
    $null=Test-BR2Dependencies $Root $Plan
    $paths=Get-BR2Paths $Root $Plan
    $null=Get-S1Files $paths.product
    $dlls=@([IO.DirectoryInfo]::new($paths.product).GetFiles('*.dll'))
    Assert-S1InputSet @($Plan.br1.inputs.productRuntimeDlls) @($dlls.Name)
    foreach($dll in $dlls){
        $source=Resolve-S1Path $paths.installed ($Plan.br1.inputs.targetTriplet+'/bin/'+$dll.Name)
        $b=[IO.File]::ReadAllBytes($source)
        Assert-S1Identity $dll.FullName $b.Length (Get-S1Hash $b)
    }
    $exe=Join-Path $paths.product clipss2.exe
    $version=[Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
    Assert-S1Value $version.ProductVersion '0.1.1' BR2Version
    Assert-S1Value $version.FileVersion '0.1.1.0' BR2Version
    return [ordered]@{result='PASS';version='0.1.1';productRuntimeDlls=17;dllBytesMatchBR1=$true;releaseApproved=$false}
}
