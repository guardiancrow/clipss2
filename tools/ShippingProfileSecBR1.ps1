#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Reuse read-only S1 byte/checkout/process primitives. Never replace S1 functions
# or globals: the historical entry point remains independently executable.
. (Join-Path $PSScriptRoot ShippingProfileS1.ps1)
$script:BR1LedgerSha='EF0EB35F031CE4424408709D0871C2A72F0FBF109A08C7295F911F40B4FFFE32'
$script:BR1Marker='share/clipss2-sec-br1-installed-v1.json'
$script:BR1Receipt='share/clipss2-sec-br1-install-receipt-v1.json'
function Read-BR1SourceInputs($Root) {
    $legacy=Read-S1SourceInputs $Root
    $path=Resolve-S1Path $Root 'cmake/security/sec-br1/source-inputs-v1.json'
    $bytes=[IO.File]::ReadAllBytes($path)
    if((Get-S1Hash $bytes) -cne $script:BR1LedgerSha){Stop-S1 BR1LedgerIdentity}
    $inputs=Read-S1StrictJson $path
    foreach($file in $inputs.files){Assert-S1Identity (Resolve-S1Path $Root $file.path) $file.size $file.sha256}
    $overlay=Resolve-S1Path $Root $inputs.overlayRoots[0]
    $files=Get-S1Files $overlay
    $wanted=@($inputs.files.path|Where-Object {$_.StartsWith($inputs.overlayRoots[0]+'/',[StringComparison]::Ordinal)}|ForEach-Object {$_.Substring($inputs.overlayRoots[0].Length+1)})
    Assert-S1InputSet $wanted @($files.Keys)
    Assert-S1DirectorySet $overlay @($files.Keys)
    $manifest=Read-S1StrictJson (Resolve-S1Path $Root 'cmake/security/sec-br1/vcpkg.json')
    # The new manifest is byte-identical to the old one: the ordered overlays
    # alone select Brotli 1.2.1. No registry baseline or other package changes.
    if((Get-S1Hash ([IO.File]::ReadAllBytes((Resolve-S1Path $Root 'cmake/security/sec-br1/vcpkg.json')))) -cne (Get-S1Hash ([IO.File]::ReadAllBytes((Resolve-S1Path $Root 'vcpkg.json'))))){Stop-S1 BR1Manifest}
    Assert-S1Value $manifest['builtin-baseline'] $inputs.builtinBaseline BR1Baseline
    return [pscustomobject]@{inputs=$inputs;managed=$legacy.managed;sourceInputsSha256=$script:BR1LedgerSha}
}
function Get-BR1Paths($Root,$Plan) {
    $paths=[ordered]@{}
    foreach($key in $Plan.inputs.outputPolicy.Keys){$paths[$key]=Resolve-S1Path $Root $Plan.inputs.outputPolicy[$key]}
    $paths.original=Resolve-S1Path $Root 'build/managed-jpegli-src'
    return $paths
}
function Assert-BR1FreshRoots($Paths) {
    foreach($key in @('vcpkg','dependencies','installed','product')){
        if([IO.File]::Exists($Paths[$key]) -or [IO.Directory]::Exists($Paths[$key])){Stop-S1 ('BR1RootExists:'+ $key)}
    }
}
function Get-BR1InstallArguments($Root,$Paths) {
    $plan=Read-BR1SourceInputs $Root
    return @('install',('--vcpkg-root='+$Paths.vcpkg),('--x-manifest-root='+(Resolve-S1Path $Root 'cmake/security/sec-br1')),
        ('--triplet='+$plan.inputs.targetTriplet),('--host-triplet='+$plan.inputs.hostTriplet),
        ('--overlay-ports='+(Resolve-S1Path $Root $plan.inputs.overlayRoots[0])),
        ('--overlay-ports='+(Resolve-S1Path $Root $plan.inputs.overlayRoots[1])),
        ('--overlay-triplets='+(Resolve-S1Path $Root triplets)),('--x-install-root='+$Paths.installed),
        ('--x-buildtrees-root='+(Join-Path $Paths.dependencies buildtrees)),
        ('--x-packages-root='+(Join-Path $Paths.dependencies packages)),
        ('--downloads-root='+(Join-Path $Paths.dependencies downloads)),
        '--binarysource=clear','--x-asset-sources=clear','--no-print-usage')
}
function Assert-BR1VcpkgSource($Root,$Paths) {
    $plan=Read-BR1SourceInputs $Root
    Assert-S1Checkout $Paths.vcpkg $plan.inputs.builtinBaseline
    foreach($package in $plan.inputs.packages){
        if($package.name -ceq 'brotli'){continue} # effective port bytes bound above
        if([IO.Directory]::Exists((Resolve-S1Path $Root ('vcpkg-overlays/shipping-privacy-v2/'+$package.name)))){continue}
        if((Get-S1VcpkgPortTree $Paths $package.name) -cne $package.portTree){Stop-S1 ('BR1PortTree:'+ $package.name)}
    }
}
function Invoke-BR1Derive($Root,$Plan) {
    $paths=Get-BR1Paths $Root $Plan
    if([IO.Directory]::Exists($paths.derived) -or [IO.File]::Exists($paths.derived)){Stop-S1 BR1DerivedExists}
    $managed=Test-S1ManagedOriginal $Root $paths.original
    [void][IO.Directory]::CreateDirectory($paths.derived)
    foreach($row in $managed.rows){
        $bytes=[IO.File]::ReadAllBytes((Resolve-S1Path $paths.original $row.path))
        foreach($change in $managed.spec.changes){if($row.path -ceq $change.path){
            if((Get-S1Hash $bytes) -cne $change.originalSha256){Stop-S1 StagePreimage}
            $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
            if([regex]::Matches($text,[regex]::Escape($change.old)).Count -ne $change.occurrences){Stop-S1 StageOccurrences}
            $bytes=[Text.UTF8Encoding]::new($false).GetBytes($text.Replace($change.old,$change.replacement))
            if((Get-S1Hash $bytes) -cne $change.derivedSha256){Stop-S1 StagePostimage}
        }}
        if((Get-S1Hash $bytes) -cne $row.derivedSha256){Stop-S1 DerivedPostimage}
        $out=Resolve-S1Path $paths.derived $row.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($out))
        $stream=[IO.File]::Open($out,[IO.FileMode]::CreateNew)
        try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    }
    Test-S1ManagedDerivative $Root $paths.derived
}
function Get-BR1Inventory($Installed) {
    $files=Get-S1Files $Installed
    return @(foreach($relative in ($files.Keys|Sort-Object -CaseSensitive)){
        if($relative -cin @($script:BR1Marker,$script:BR1Receipt)){continue}
        $bytes=[IO.File]::ReadAllBytes($files[$relative])
        [ordered]@{path=$relative;size=$bytes.Length;sha256=(Get-S1Hash $bytes)}
    })
}
function Get-BR1Marker($Plan) {
    return [ordered]@{schemaVersion=1;markerId='clipss2-sec-br1-installed-v1';
        contractId=$Plan.inputs.inputId;sourceInputsSha256=$Plan.sourceInputsSha256;
        targetTriplet=$Plan.inputs.targetTriplet}
}
function Assert-BR1Integer($Value,$Expected,$Code) {
    if(($Value -isnot [int] -and $Value -isnot [long]) -or $Value -ne $Expected){Stop-S1 $Code}
}
function Test-BR1Payload($Root,$Plan) {
    $paths=Get-BR1Paths $Root $Plan
    $installed=$paths.installed
    $files=Get-S1Files $installed
    # Same package-list and nonempty payload checks as S1, using the new plan.
    Test-S1DependencyPayload $installed $Plan
    $records=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $status=[IO.File]::ReadAllText((Resolve-S1Path $installed 'vcpkg/status'))
    foreach($paragraph in ($status -split '(?:\r?\n){2,}')){
        if([string]::IsNullOrWhiteSpace($paragraph)){continue}
        $fields=@{};$lastField=''
        foreach($line in ($paragraph -split '\r?\n')){
            if(!$line){continue}
            if($line.StartsWith(' ') -and $lastField -ceq 'Description'){continue}
            if($line -notmatch '^([A-Za-z-]+): (.*)$' -or $fields.ContainsKey($Matches[1])){Stop-S1 BR1Status}
            $lastField=$Matches[1];$fields[$lastField]=$Matches[2]
        }
        $packages=@($Plan.inputs.packages|Where-Object name -CEQ $fields.Package)
        if($packages.Count -ne 1){Stop-S1 BR1Status}
        $package=$packages[0]
        $triplet=if($package.role -ceq 'target'){$Plan.inputs.targetTriplet}else{$Plan.inputs.hostTriplet}
        if($fields.Architecture -cne $triplet -or $fields.Status -cne 'install ok installed'){Stop-S1 BR1Status}
        if($fields.ContainsKey('Feature')){continue}
        $version=$package.version.Split('#')
        $port=if($version.Count -eq 2){$version[1]}else{'0'}
        $actualPort=if($fields.ContainsKey('Port-Version')){$fields['Port-Version']}else{'0'}
        if($fields.Version -cne $version[0] -or $port -cne $actualPort -or !$records.Add($fields.Package)){Stop-S1 BR1Version}
    }
    Assert-S1InputSet @($Plan.inputs.packages.name) @($records)
    $prefix=$Plan.inputs.targetTriplet+'/bin/'
    $dlls=@($files.Keys|Where-Object {$_.StartsWith($prefix,[StringComparison]::Ordinal) -and $_.EndsWith('.dll',[StringComparison]::OrdinalIgnoreCase)}|ForEach-Object {$_.Substring($prefix.Length)})
    Assert-S1InputSet @($Plan.inputs.runtimeDlls) $dlls
    # The copied upstream copyright must be the already preserved MIT original.
    $notice=Resolve-S1Path $installed ($Plan.inputs.targetTriplet+'/share/brotli/copyright')
    $original=[IO.File]::ReadAllBytes((Resolve-S1Path $Root 'licenses/brotli/copyright'))
    Assert-S1Identity $notice $original.Length (Get-S1Hash $original)
}
function Test-BR1Installed($Root,$Plan) {
    $paths=Get-BR1Paths $Root $Plan
    $marker=Read-S1StrictJson (Resolve-S1Path $paths.installed $script:BR1Marker)
    $wanted=Get-BR1Marker $Plan
    Assert-S1Fields $marker @($wanted.Keys)
    Assert-BR1Integer $marker.schemaVersion 1 BR1Marker
    foreach($key in @('markerId','contractId','sourceInputsSha256','targetTriplet')){Assert-S1Value $marker[$key] $wanted[$key] BR1Marker}
    Test-BR1Receipt $Root $Plan
    return [ordered]@{result='PASS';contractId=$Plan.inputs.inputId;packages=16;runtimeDlls=19;
        inventoryVerified=$true;abiVerified=$false;binaryIdentityVerified=$false;binaryReleaseApproved=$false}
}
function Test-BR1Receipt($Root,$Plan) {
    $paths=Get-BR1Paths $Root $Plan
    $receipt=Read-S1StrictJson (Resolve-S1Path $paths.installed $script:BR1Receipt)
    Assert-S1Fields $receipt @('schemaVersion','receiptId','sourceInputsSha256','vcpkgBaseline','vcpkgToolSha256','installExitCode','inventory')
    Assert-BR1Integer $receipt.schemaVersion 1 BR1Receipt
    Assert-BR1Integer $receipt.installExitCode 0 BR1Receipt
    Assert-S1Value $receipt.receiptId 'clipss2-sec-br1-install-receipt-v1' BR1Receipt
    Assert-S1Value $receipt.sourceInputsSha256 $Plan.sourceInputsSha256 BR1Receipt
    Assert-S1Value $receipt.vcpkgBaseline $Plan.inputs.builtinBaseline BR1Receipt
    Assert-S1String $receipt.vcpkgToolSha256
    if($receipt.vcpkgToolSha256 -cnotmatch '^[A-F0-9]{64}$' -or $receipt.inventory -isnot [array] -or !$receipt.inventory.Count){Stop-S1 BR1Receipt}
    foreach($item in $receipt.inventory){
        Assert-S1Fields $item @('path','size','sha256')
        Assert-S1Identity (Resolve-S1Path $paths.installed $item.path) $item.size $item.sha256
    }
    $actual=@(Get-BR1Inventory $paths.installed)
    Assert-S1InputSet @($actual.path) @($receipt.inventory.path)
    Test-BR1Payload $Root $Plan
}
function Invoke-BR1Install($Root,[switch]$BuildToolsEligibilityConfirmed) {
    if(!$BuildToolsEligibilityConfirmed){Stop-S1 BuildToolsEligibilityUnconfirmed}
    if(![OperatingSystem]::IsWindows() -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::X64){Stop-S1 InstallHost}
    $plan=Read-BR1SourceInputs $Root
    $paths=Get-BR1Paths $Root $plan
    Assert-BR1FreshRoots $paths
    $null=Test-S1ManagedOriginal $Root $paths.original
    $null=Test-S1ManagedDerivative $Root $paths.derived
    $git=(Get-Command git -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
    New-S1OwnedDirectory $paths.dependencies
    Write-S1NewJson (Join-Path $paths.dependencies owner.json) ([ordered]@{ownerId='clipss2-sec-br1-dependencies-v1';sourceInputsSha256=$plan.sourceInputsSha256;state='UNADMITTED';reuseAllowed=$false})
    foreach($dir in @('logs','tmp','downloads','registries','buildtrees','packages')){New-S1OwnedDirectory (Join-Path $paths.dependencies $dir)}
    New-S1OwnedDirectory $paths.vcpkg
    New-S1OwnedDirectory $paths.installed
    $step='git-init'
    try{
        $null=Invoke-S1InstallCommand $step $git @('-c','init.templateDir=','init',$paths.vcpkg) $Root $paths
        $step='git-fetch'
        $null=Invoke-S1InstallCommand $step $git @('-C',$paths.vcpkg,'-c','protocol.file.allow=never','-c','protocol.ext.allow=never','fetch','--depth=1','https://github.com/microsoft/vcpkg.git',$plan.inputs.builtinBaseline) $Root $paths
        $step='git-checkout'
        $null=Invoke-S1InstallCommand $step $git @('-C',$paths.vcpkg,'-c','core.autocrlf=false','checkout','--detach',$plan.inputs.builtinBaseline) $Root $paths
        $step='vcpkg-source'
        Assert-BR1VcpkgSource $Root $paths
        $step='bootstrap'
        $null=Invoke-S1InstallCommand $step (Join-Path $PSHOME pwsh.exe) @('-NoProfile','-File',(Join-Path $paths.vcpkg scripts/bootstrap.ps1),'-disableMetrics') $paths.vcpkg $paths
        $tool=Resolve-S1Path $paths.vcpkg vcpkg.exe
        $toolSha=Get-S1Hash ([IO.File]::ReadAllBytes($tool))
        $step='install'
        $null=Invoke-S1InstallCommand $step $tool (Get-BR1InstallArguments $Root $paths) $Root $paths
        $step='acceptance'
        $plan=Read-BR1SourceInputs $Root
        Assert-BR1VcpkgSource $Root $paths
        $null=Test-S1ManagedOriginal $Root $paths.original
        $null=Test-S1ManagedDerivative $Root $paths.derived
        if((Get-S1Hash ([IO.File]::ReadAllBytes($tool))) -cne $toolSha){Stop-S1 VcpkgToolChanged}
        Test-BR1Payload $Root $plan
        foreach($relative in @($script:BR1Marker,$script:BR1Receipt)){
            $file=Resolve-S1Path $paths.installed $relative
            if([IO.File]::Exists($file) -or [IO.Directory]::Exists($file)){Stop-S1 PrematureInstallMarker}
        }
        [void][IO.Directory]::CreateDirectory((Join-Path $paths.installed share))
        $receipt=[ordered]@{schemaVersion=1;receiptId='clipss2-sec-br1-install-receipt-v1';
            sourceInputsSha256=$plan.sourceInputsSha256;vcpkgBaseline=$plan.inputs.builtinBaseline;
            vcpkgToolSha256=$toolSha;installExitCode=0;inventory=@(Get-BR1Inventory $paths.installed)}
        Write-S1NewJson (Resolve-S1Path $paths.installed $script:BR1Receipt) $receipt
        Test-BR1Receipt $Root $plan
        $pending=Join-Path $paths.dependencies install-marker.pending.json
        Write-S1NewJson $pending (Get-BR1Marker $plan)
        [IO.File]::Move($pending,(Resolve-S1Path $paths.installed $script:BR1Marker),$false)
        Test-BR1Installed $Root $plan
    }catch{
        throw [IO.InvalidDataException]::new(('SEC-BR1:InstallFailed:'+ $step+'; partial roots preserved; '+$_.Exception.Message),$_.Exception)
    }
}
function Invoke-BR1Configure($Root,$Plan) {
    $paths=Get-BR1Paths $Root $Plan
    $null=Test-BR1Installed $Root $Plan
    if([IO.Directory]::Exists($paths.product) -or [IO.File]::Exists($paths.product)){Stop-S1 BR1ProductExists}
    $rootCmake=$Root.Replace('\','/')
    $arguments=@('-S',$rootCmake,'-B',$paths.product,'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release','-DCLIPSS2_SHIPPING_PRIVACY=ON','-DCLIPSS2_SHIPPING_PROFILE=v2',
        '-DCLIPSS2_PUBLICATION_SCOPE=sec-br1','-DVCPKG_MANIFEST_INSTALL=OFF',
        '-DVCPKG_TARGET_TRIPLET=x64-windows-shipping-privacy','-DVCPKG_HOST_TRIPLET=x64-windows',
        '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF','-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
        '-DCLIPSS2_BUILD_JPEG_BENCH=OFF','-DCLIPSS2_BUILD_LOSSY_EVAL=OFF',
        ('-DCMAKE_TOOLCHAIN_FILE='+$rootCmake+'/build/sec-br1-r1/vcpkg/scripts/buildsystems/vcpkg.cmake'),
        ('-DVCPKG_MANIFEST_DIR='+$rootCmake+'/cmake/security/sec-br1'),
        ('-DVCPKG_OVERLAY_PORTS='+$rootCmake+'/vcpkg-overlays/security-sec-br1;'+$rootCmake+'/vcpkg-overlays/shipping-privacy-v2'),
        ('-DVCPKG_OVERLAY_TRIPLETS='+$rootCmake+'/triplets'),
        ('-DVCPKG_INSTALLED_DIR='+$rootCmake+'/build/sec-br1-r1/installed'),
        ('-DCLIPSS2_JPEGLI_SOURCE_DIR='+$rootCmake+'/build/managed-jpegli-src'),
        ('-DCLIPSS2_DERIVED_JPEGLI_DIR='+$rootCmake+'/build/sec-br1-r1/managed-jpegli'))
    $cmake=(Get-Command cmake -CommandType Application|Select-Object -First 1).Source
    $step='configure-'+[guid]::NewGuid().ToString('N')
    $null=Invoke-S1InstallCommand $step $cmake $arguments $Root $paths
}
function Test-BR1Product($Root,$Plan) {
    $paths=Get-BR1Paths $Root $Plan
    $null=Test-BR1Installed $Root $Plan
    $null=Get-S1Files $paths.product
    $dlls=@([IO.DirectoryInfo]::new($paths.product).GetFiles('*.dll'))
    Assert-S1InputSet @($Plan.inputs.productRuntimeDlls) @($dlls.Name)
    foreach($dll in $dlls){
        $installed=Resolve-S1Path $paths.installed ($Plan.inputs.targetTriplet+'/bin/'+$dll.Name)
        $bytes=[IO.File]::ReadAllBytes($installed)
        Assert-S1Identity $dll.FullName $bytes.Length (Get-S1Hash $bytes)
    }
    if(![IO.File]::Exists((Join-Path $paths.product clipss2.exe))){Stop-S1 BR1ProductMissing}
    return [ordered]@{result='PASS';productRuntimeDlls=17;installedRuntimeDlls=19;
        dllBytesMatchInstalled=$true;binaryReleaseApproved=$false}
}
