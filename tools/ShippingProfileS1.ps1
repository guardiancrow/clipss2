#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
. (Join-Path $PSScriptRoot ShippingPrivacyS1.ps1)
$script:S1InputsSha='80BD9B28EF236F29E31BC12B1D6C16120C9F7E60AC0ACF4F861691C6DF5C64AB'
$script:S1BoundPaths=@(
    'vcpkg-overlays/shipping-privacy-v2/highway/diagnostic-file-id.patch','vcpkg-overlays/shipping-privacy-v2/highway/portfile.cmake','vcpkg-overlays/shipping-privacy-v2/highway/usage','vcpkg-overlays/shipping-privacy-v2/highway/vcpkg.json',
    'vcpkg-overlays/shipping-privacy-v2/libjxl/diagnostic-file-id.patch','vcpkg-overlays/shipping-privacy-v2/libjxl/fix-dependencies.patch','vcpkg-overlays/shipping-privacy-v2/libjxl/modified-file-notice.patch','vcpkg-overlays/shipping-privacy-v2/libjxl/msvc-remove-libm.patch','vcpkg-overlays/shipping-privacy-v2/libjxl/portfile.cmake','vcpkg-overlays/shipping-privacy-v2/libjxl/vcpkg.json',
    'vcpkg-overlays/shipping-privacy-v2/libyuv/cmake.diff','vcpkg-overlays/shipping-privacy-v2/libyuv/libyuv-config.cmake','vcpkg-overlays/shipping-privacy-v2/libyuv/portfile.cmake','vcpkg-overlays/shipping-privacy-v2/libyuv/usage','vcpkg-overlays/shipping-privacy-v2/libyuv/vcpkg.json',
    'cmake/privacy/managed-origin-s1-v1.json','cmake/privacy/managed-origin-s1-v1.tsv','cmake/privacy/managed-diagnostics-v2.json','cmake/privacy/managed-sources-v2.tsv','cmake/privacy/dependency-diagnostics-v2.json','cmake/privacy/DependencyPorts-v2.cmake','cmake/privacy/DependencyProfile-s1-v1.cmake','triplets/x64-windows-shipping-privacy.cmake','vcpkg.json','vcpkg-configuration.json','CMakePresets.json','.gitattributes','cmake/privacy/.gitattributes')
$script:S1OutputPolicy=[ordered]@{vcpkg='build/shipping-vcpkg-s1-v2';dependencies='build/shipping-dependencies-s1-v2';installed='build/vcpkg_installed-s1-v2';derived='build/managed-jpegli-s1-v2';product='build/windows-x64-s1-v2'}
function Assert-S1SourceInputSchema($Inputs) {
    Assert-S1Fields $Inputs @('schemaVersion','inputId','sourceScopeId','profile','shippingPrivacy','builtinBaseline','targetTriplet','hostTriplet','overlayRoot','packages','files','outputPolicy')
    if(($Inputs.schemaVersion -isnot [long] -and $Inputs.schemaVersion -isnot [int]) -or $Inputs.schemaVersion -ne 1){Stop-S1 InputSchema}
    $wanted=@{inputId='clipss2-source-inputs-s1-v1';sourceScopeId='clipss2-source-s1-v1';profile='v2';shippingPrivacy='ON';builtinBaseline='04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4';targetTriplet='x64-windows-shipping-privacy';hostTriplet='x64-windows';overlayRoot='vcpkg-overlays/shipping-privacy-v2'}
    foreach($key in $wanted.Keys){Assert-S1Value $Inputs[$key] $wanted[$key] InputSelection}
    if($Inputs.files -isnot [array] -or $Inputs.files.Count -ne 28){Stop-S1 InputCount}
    foreach($file in $Inputs.files){
        Assert-S1Fields $file @('path','size','sha256');Assert-S1RelativePath $file.path;Assert-S1String $file.sha256
        if(($file.size -isnot [long] -and $file.size -isnot [int]) -or $file.size -lt 0 -or $file.sha256 -cnotmatch '^[A-F0-9]{64}$'){Stop-S1 InputRecord}
    }
    Assert-S1InputSet $script:S1BoundPaths @($Inputs.files.path)
    if($Inputs.packages -isnot [array] -or $Inputs.packages.Count -ne 16){Stop-S1 Packages}
    foreach($package in $Inputs.packages){
        Assert-S1Fields $package @('name','version','portTree','role','upstream')
        foreach($key in @('name','version','portTree','role')){Assert-S1String $package[$key]}
        if($package.upstream -isnot [array]){Stop-S1 Packages}
        foreach($url in $package.upstream){Assert-S1String $url}
    }
    $packageText=ConvertTo-Json -InputObject $Inputs.packages -Depth 8 -Compress
    if((Get-S1Hash ([Text.Encoding]::UTF8.GetBytes($packageText))) -cne 'FF2913CA66BFCB864A321B15B00AB35E08EFDF98651E342FB9BCA0AB855B4F0A'){Stop-S1 Packages}
    Assert-S1Fields $Inputs.outputPolicy @('vcpkg','dependencies','installed','derived','product')
    foreach($key in $script:S1OutputPolicy.Keys){Assert-S1Value $Inputs.outputPolicy[$key] $script:S1OutputPolicy[$key] OutputPolicy}
}
function Assert-S1Presets($Presets) {
    Assert-S1Fields $Presets @('version','configurePresets','buildPresets')
    if(($Presets.version -isnot [long] -and $Presets.version -isnot [int]) -or $Presets.version -ne 8){Stop-S1 Preset}
    if($Presets.configurePresets -isnot [array] -or $Presets.configurePresets.Count -ne 2 -or $Presets.buildPresets -isnot [array] -or $Presets.buildPresets.Count -ne 1){Stop-S1 Preset}
    $base=$Presets.configurePresets[0];$public=$Presets.configurePresets[1];$build=$Presets.buildPresets[0]
    Assert-S1Fields $base @('name','hidden','generator','toolchainFile','cacheVariables')
    Assert-S1Value $base.name '_s1-release-base' Preset
    if($base.hidden -isnot [bool] -or !$base.hidden){Stop-S1 Preset}
    Assert-S1Value $base.generator 'Ninja' Generator
    Assert-S1Value $base.toolchainFile '${sourceDir}/build/shipping-vcpkg-s1-v2/scripts/buildsystems/vcpkg.cmake' OutputPolicy
    $baseCache=@{CMAKE_BUILD_TYPE='Release';CMAKE_CXX_STANDARD='23';CMAKE_CXX_STANDARD_REQUIRED='ON';CMAKE_CXX_EXTENSIONS='OFF'}
    Assert-S1Fields $base.cacheVariables @($baseCache.Keys)
    foreach($key in $baseCache.Keys){Assert-S1Value $base.cacheVariables[$key] $baseCache[$key] Configuration}
    Assert-S1Fields $public @('name','inherits','binaryDir','cacheVariables')
    Assert-S1Value $public.name 'windows-x64-shipping-v2' Preset
    Assert-S1Value $public.inherits '_s1-release-base' Preset
    Assert-S1Value $public.binaryDir '${sourceDir}/build/windows-x64-s1-v2' OutputPolicy
    $cache=@{CLIPSS2_SHIPPING_PRIVACY='ON';CLIPSS2_SHIPPING_PROFILE='v2';CLIPSS2_PUBLICATION_SCOPE='s1-v1';CLIPSS2_JPEGLI_SOURCE_DIR='${sourceDir}/build/managed-jpegli-src';CLIPSS2_DERIVED_JPEGLI_DIR='${sourceDir}/build/managed-jpegli-s1-v2';VCPKG_INSTALLED_DIR='${sourceDir}/build/vcpkg_installed-s1-v2';VCPKG_TARGET_TRIPLET='x64-windows-shipping-privacy';VCPKG_HOST_TRIPLET='x64-windows';VCPKG_OVERLAY_PORTS='${sourceDir}/vcpkg-overlays/shipping-privacy-v2';VCPKG_OVERLAY_TRIPLETS='${sourceDir}/triplets';VCPKG_MANIFEST_INSTALL='OFF';CMAKE_FIND_USE_PACKAGE_REGISTRY='OFF';CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY='OFF'}
    Assert-S1Fields $public.cacheVariables @($cache.Keys)
    foreach($key in $cache.Keys){Assert-S1Value $public.cacheVariables[$key] $cache[$key] PresetSelection}
    Assert-S1Fields $build @('name','configurePreset','configuration')
    Assert-S1Value $build.name 'windows-x64-shipping-v2' Preset
    Assert-S1Value $build.configurePreset 'windows-x64-shipping-v2' Preset
    Assert-S1Value $build.configuration 'Release' Configuration
}
function Read-S1SourceInputs($Root) {
    $file=Resolve-S1Path $Root 'cmake/privacy/source-inputs-s1-v1.json'
    $inputs=Read-S1StrictJson $file
    Assert-S1SourceInputSchema $inputs
    Assert-S1Identity $file 9645 $script:S1InputsSha
    foreach($input in $inputs.files){Assert-S1Identity (Resolve-S1Path $Root $input.path) $input.size $input.sha256}
    $overlay=Get-S1Files (Resolve-S1Path $Root $inputs.overlayRoot)
    $wantedOverlay=@($script:S1BoundPaths|Where-Object {$_.StartsWith($inputs.overlayRoot+'/',[StringComparison]::Ordinal)})
    Assert-S1InputSet $wantedOverlay @($overlay.Keys|ForEach-Object {$inputs.overlayRoot+'/'+$_})
    Assert-S1DirectorySet (Resolve-S1Path $Root $inputs.overlayRoot) @($overlay.Keys)
    $config=Read-S1StrictJson (Resolve-S1Path $Root vcpkg-configuration.json)
    Assert-S1Fields $config @('overlay-ports')
    if($config['overlay-ports'] -isnot [array] -or $config['overlay-ports'].Count -ne 1){Stop-S1 Overlay}
    Assert-S1Value $config['overlay-ports'][0] $inputs.overlayRoot Overlay
    $manifest=Read-S1StrictJson (Resolve-S1Path $Root vcpkg.json)
    Assert-S1Value $manifest['builtin-baseline'] $inputs.builtinBaseline Baseline
    if($manifest.Contains('overrides')){Stop-S1 Baseline}
    Assert-S1Presets (Read-S1StrictJson (Resolve-S1Path $Root CMakePresets.json))
    $managed=Read-S1Managed $Root
    return [pscustomobject]@{inputs=$inputs;managed=$managed;sourceInputsSha256=$script:S1InputsSha}
}
function Get-S1MarkerExpected($Plan) {
    return [ordered]@{schemaVersion=1;markerId='clipss2-s1-installed-profile-v1';sourceScopeId='clipss2-source-s1-v1';profile='v2';sourceInputsSha256=$Plan.sourceInputsSha256;originContractSha256=$Plan.managed.origin.originSha256;managedSpecSha256=$Plan.managed.specSha256;managedManifestSha256=$Plan.managed.manifestSha256;targetTriplet='x64-windows-shipping-privacy'}
}
function Assert-S1Marker($Marker,$Plan) {
    $wanted=Get-S1MarkerExpected $Plan
    Assert-S1Fields $Marker @($wanted.Keys)
    if(($Marker.schemaVersion -isnot [int] -and $Marker.schemaVersion -isnot [long]) -or $Marker.schemaVersion -ne 1){Stop-S1 MarkerIdentity}
    foreach($key in @($wanted.Keys|Where-Object {$_ -cne 'schemaVersion'})){Assert-S1Value $Marker[$key] $wanted[$key] MarkerIdentity}
}
function Test-S1InstalledStructure($Root,$InstalledDir,$Plan) {
    $installed=Get-S1ExpectedRoot $Root 'build/vcpkg_installed-s1-v2' $InstalledDir
    foreach($relative in @('x64-windows-shipping-privacy/bin','x64-windows-shipping-privacy/lib','x64-windows-shipping-privacy/include','x64-windows-shipping-privacy/share','x64-windows/tools','x64-windows/share','vcpkg/info')){
        if(![IO.Directory]::Exists((Resolve-S1Path $installed $relative))){Stop-S1 InstalledStructure}
    }
    # Check all descendants before reading status; marker alone is insufficient.
    $null=Get-S1Files $installed
    $statusPath=Resolve-S1Path $installed 'vcpkg/status'
    if(![IO.File]::Exists($statusPath)){Stop-S1 InstalledStructure}
    $status=[Text.UTF8Encoding]::new($false,$true).GetString([IO.File]::ReadAllBytes($statusPath))
    $records=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach($paragraph in ($status -split '(?:\r?\n){2,}')){
        if([string]::IsNullOrWhiteSpace($paragraph)){continue}
        $fields=@{}
        $lastField=''
        foreach($line in ($paragraph -split '\r?\n')){
            if(!$line){continue}
            if($line.StartsWith(' ') -and $lastField -ceq 'Description'){continue}
            if($line -notmatch '^([A-Za-z-]+): (.*)$' -or $fields.ContainsKey($Matches[1])){Stop-S1 InstalledStatus}
            $lastField=$Matches[1];$fields[$lastField]=$Matches[2]
        }
        if(!$fields.ContainsKey('Package') -or !$fields.ContainsKey('Architecture')){Stop-S1 InstalledStatus}
        $package=@($Plan.inputs.packages|Where-Object name -CEQ $fields.Package)
        if($package.Count -ne 1){Stop-S1 InstalledStatus}
        $architecture=if($package[0].role -ceq 'target'){$Plan.inputs.targetTriplet}else{$Plan.inputs.hostTriplet}
        if($fields.Architecture -cne $architecture -or !$fields.ContainsKey('Status') -or $fields.Status -cne 'install ok installed'){Stop-S1 InstalledStatus}
        if($fields.ContainsKey('Feature')){continue}
        $version=$package[0].version.Split('#')
        if(!$fields.ContainsKey('Version') -or $fields.Version -cne $version[0]){Stop-S1 InstalledStatus}
        $port=if($version.Count -eq 2){$version[1]}else{'0'}
        $actualPort=if($fields.ContainsKey('Port-Version')){$fields['Port-Version']}else{'0'}
        if($actualPort -cne $port -or $records.ContainsKey($fields.Package)){Stop-S1 InstalledStatus}
        $records.Add($fields.Package,$fields)
    }
    if($records.Count -ne 16){Stop-S1 InstalledStatus}
    return [pscustomobject]@{result='PASS';markerValidated=$false;packageStatusRecords=16;actualInstallPerformed=$false;abiVerified=$false;binaryIdentityVerified=$false}
}
function Test-S1Installed($Root,$InstalledDir,$Plan) {
    $installed=Get-S1ExpectedRoot $Root 'build/vcpkg_installed-s1-v2' $InstalledDir
    Assert-S1Marker (Read-S1StrictJson (Resolve-S1Path $installed 'share/clipss2-source-profile-s1-v1.json')) $Plan
    $result=Test-S1InstalledStructure $Root $installed $Plan
    $null=Test-S1InstallReceipt $installed $Plan
    $result.markerValidated=$true
    return $result
}
function Get-S1Plan($Root,$Profile,$Scope,$Privacy,$Configuration,$Architecture) {
    Assert-S1Selection $Profile $Scope $Privacy $Configuration $Architecture
    $plan=Read-S1SourceInputs $Root
    $states=@(foreach($key in $script:S1OutputPolicy.Keys){$path=Resolve-S1Path $Root $script:S1OutputPolicy[$key];[ordered]@{role=$key;relative=$script:S1OutputPolicy[$key];state=$(if([IO.File]::Exists($path)){Stop-S1 OutputRoot}elseif([IO.Directory]::Exists($path)){'PRESENT_UNADMITTED'}else{'ABSENT'})}})
    return [ordered]@{result='PASS';mode='PlanOnly';sourceScopeId='clipss2-source-s1-v1';profile='v2';sourceInputsSha256=$plan.sourceInputsSha256;boundInputs=28;packages=$plan.inputs.packages;targetTriplet=$plan.inputs.targetTriplet;hostTriplet=$plan.inputs.hostTriplet;overlayRoot=$plan.inputs.overlayRoot;originalTuples=1481;checkouts=4;gitlinks=9;v2Stages=3;outputRoots=$states;verified=@('28 raw input identities','origin contract/1481 original tuples','fixed v2 metadata and 3-stage postimages','preset/config/profile/output policy');notVerified=@('real dependency install/ABI/binaries','actual source checkouts or derived output bytes in PlanOnly','compiler/toolchain/generator runtime','ownership or reusability of existing output directories');sideEffects='NONE';vcpkgInvoked=$false;compilerProbe=$false;actualInstallPerformed=$false;installedValidated=$false}
}
. (Join-Path $PSScriptRoot S1DependencyInstall.ps1)
