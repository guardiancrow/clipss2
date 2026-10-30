#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Offline synthetic fixtures. No compiler, download, or production output reuse.
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=(Resolve-Path "$PSScriptRoot/..").Path
. "$source/tools/ShippingProfileSecBR1.ps1"
Assert-S1NoReparse $EvidenceRoot
$run=Join-Path $EvidenceRoot ('sec-br1-'+[guid]::NewGuid().ToString('N'))
New-S1OwnedDirectory $run
$plan=Read-BR1SourceInputs $source
$legacy=Read-S1SourceInputs $source
$utf8=[Text.UTF8Encoding]::new($false)
$results=[Collections.Generic.List[object]]::new()
function Case($Name,$Expected,$Body){
    $actual='ACCEPT'
    try{& $Body | Out-Null}catch{$actual=$_.Exception.Message}
    $ok=if($Expected){$actual.Contains($Expected,[StringComparison]::Ordinal)}else{$actual -ceq 'ACCEPT'}
    $results.Add([ordered]@{name=$Name;result=$(if($ok){'PASS'}else{'FAIL'});actual=$actual})
}
function New-View($Name){
    $view=Join-Path $run $Name
    $paths=@($legacy.inputs.files.path)+@($plan.inputs.files.path)+@(
        'cmake/privacy/source-inputs-s1-v1.json','cmake/security/sec-br1/source-inputs-v1.json',
        'tools/ShippingProfileSecBR1.ps1','tools/build_shipping_dependencies_sec_br1.ps1')
    foreach($relative in ($paths|Select-Object -Unique)){
        $dest=Join-Path $view $relative
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        [IO.File]::Copy((Join-Path $source $relative),$dest,$false)
    }
    return $view
}
function New-Payload($Name){
    $view=New-View $Name
    $paths=Get-BR1Paths $view $plan
    $installed=$paths.installed
    foreach($dir in @('share','vcpkg/info','x64-windows-shipping-privacy/bin','x64-windows-shipping-privacy/lib',
        'x64-windows-shipping-privacy/include','x64-windows/tools/pkgconf')){
        [void][IO.Directory]::CreateDirectory((Join-Path $installed $dir))
    }
    $status=@(foreach($package in $plan.inputs.packages){
        $triplet=if($package.role -ceq 'target'){$plan.inputs.targetTriplet}else{$plan.inputs.hostTriplet}
        $version=$package.version.Split('#')
        $port=if($version.Count -eq 2){$version[1]}else{'0'}
        $relative=$triplet+'/share/'+$package.name+'/fixture.txt'
        $file=Join-Path $installed $relative
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))
        [IO.File]::WriteAllText($file,'synthetic fixture, not a real library',$utf8)
        [IO.File]::WriteAllText((Join-Path $installed ('vcpkg/info/'+$package.name+'_'+$package.version+'_'+$triplet+'.list')),$relative+"`n",$utf8)
        "Package: $($package.name)`nVersion: $($version[0])`nPort-Version: $port`nArchitecture: $triplet`nStatus: install ok installed`n"
    })
    [IO.File]::WriteAllText((Join-Path $installed 'vcpkg/status'),($status -join "`n"),$utf8)
    foreach($dll in $plan.inputs.runtimeDlls){[IO.File]::WriteAllText((Join-Path $installed ($plan.inputs.targetTriplet+'/bin/'+$dll)),'synthetic DLL fixture',$utf8)}
    foreach($relative in @('x64-windows-shipping-privacy/lib/fixture.lib','x64-windows-shipping-privacy/include/fixture.h','x64-windows/tools/pkgconf/pkgconf.exe')){
        [IO.File]::WriteAllText((Join-Path $installed $relative),'fixture',$utf8)
    }
    [IO.File]::Copy((Join-Path $view 'licenses/brotli/copyright'),(Join-Path $installed ($plan.inputs.targetTriplet+'/share/brotli/copyright')),$false)
    return $view
}
function Seal-Payload($View){
    $paths=Get-BR1Paths $View $plan
    $receipt=[ordered]@{schemaVersion=1;receiptId='clipss2-sec-br1-install-receipt-v1';sourceInputsSha256=$plan.sourceInputsSha256;
        vcpkgBaseline=$plan.inputs.builtinBaseline;vcpkgToolSha256=('A'*64);installExitCode=0;inventory=@(Get-BR1Inventory $paths.installed)}
    Write-S1NewJson (Join-Path $paths.installed $script:BR1Receipt) $receipt
    Write-S1NewJson (Join-Path $paths.installed $script:BR1Marker) (Get-BR1Marker $plan)
}
function New-Product($View){
    $paths=Get-BR1Paths $View $plan
    [void][IO.Directory]::CreateDirectory($paths.product)
    foreach($name in $plan.inputs.productRuntimeDlls){
        [IO.File]::Copy((Join-Path $paths.installed ($plan.inputs.targetTriplet+'/bin/'+$name)),(Join-Path $paths.product $name),$false)
    }
    [IO.File]::WriteAllText((Join-Path $paths.product clipss2.exe),'synthetic product, not an executable',$utf8)
}
Case 'legacy ledger remains valid' '' { Read-S1SourceInputs $source }
Case 'only Brotli changes among 16 packages' '' {
    if($plan.inputs.packages.Count -ne 16){throw 'Wrong closure'}
    foreach($package in $plan.inputs.packages){
        $old=@($legacy.inputs.packages|Where-Object name -CEQ $package.name)[0]
        if($package.name -ceq 'brotli'){
            if($old.version -cne '1.2.0' -or $package.version -cne '1.2.1'){throw 'Wrong Brotli selection'}
        }elseif((ConvertTo-Json $old -Compress -Depth 8) -cne (ConvertTo-Json $package -Compress -Depth 8)){throw 'Unrelated update'}
    }
}
Case 'official Brotli port tree identity' '' {
    $wanted=@{ 'install.patch'='04990fb08c5ce5e584d16072e9805e89a1b35af7';'pkgconfig.patch'='77e74fbc5b48e377f0af5acd80653813c1a31fba';
        'portfile.cmake'='650d25d51e93c1903a56550b90b87fda0732846d';'usage'='d3dde761ef4d40d1996a3b60fc0b0ed292cd6552';'vcpkg.json'='abaeb1caa82cfc300714347fea5c1a436ac58d04'}
    foreach($name in $wanted.Keys){if((Get-S1BlobOid ([IO.File]::ReadAllBytes((Join-Path $source ('vcpkg-overlays/security-sec-br1/brotli/'+$name))))) -cne $wanted[$name]){throw 'Official port bytes differ'}}
}
Case 'new ledger tampering rejected' 'BR1LedgerIdentity' {
    $v=New-View ledger
    [IO.File]::AppendAllText((Join-Path $v 'cmake/security/sec-br1/source-inputs-v1.json'),' ',$utf8)
    Read-BR1SourceInputs $v
}
Case 'historical input tampering rejected' 'RawIdentity' {
    $v=New-View legacy
    [IO.File]::AppendAllText((Join-Path $v 'CMakePresets.json'),' ',$utf8)
    Read-BR1SourceInputs $v
}
Case 'overlay extra file rejected' 'InputSet' {
    $v=New-View extra
    [IO.File]::WriteAllText((Join-Path $v 'vcpkg-overlays/security-sec-br1/brotli/extra.patch'),'extra',$utf8)
    Read-BR1SourceInputs $v
}
Case 'overlay port tampering rejected' 'RawIdentity' {
    $v=New-View port
    [IO.File]::AppendAllText((Join-Path $v 'vcpkg-overlays/security-sec-br1/brotli/vcpkg.json'),' ',$utf8)
    Read-BR1SourceInputs $v
}
Case 'eligibility gate creates no output' '' {
    $v=New-View eligibility
    $errorText=''
    try{Invoke-BR1Install $v}catch{$errorText=$_.Exception.Message}
    if(!$errorText.Contains('BuildToolsEligibilityUnconfirmed') -or [IO.Directory]::Exists((Join-Path $v build))){throw 'Eligibility gate wrote outputs or accepted unconfirmed caller'}
}
Case 'existing even empty installed root rejected' 'BR1RootExists' {
    $v=New-View occupied
    $paths=Get-BR1Paths $v $plan
    [void][IO.Directory]::CreateDirectory($paths.installed)
    Assert-BR1FreshRoots $paths
}
Case 'ordered CLI overlays and inherited environment isolation' '' {
    $v=New-View arguments
    $paths=Get-BR1Paths $v $plan
    $arguments=Get-BR1InstallArguments $v $paths
    $overlays=@($arguments|Where-Object {$_ -clike '--overlay-ports=*'})
    if($overlays.Count -ne 2 -or $overlays[0] -cne ('--overlay-ports='+(Resolve-S1Path $v $plan.inputs.overlayRoots[0])) -or $overlays[1] -cne ('--overlay-ports='+(Resolve-S1Path $v $plan.inputs.overlayRoots[1]))){throw 'Overlay order changed'}
    $saved=$env:VCPKG_OVERLAY_PORTS
    try{
        $env:VCPKG_OVERLAY_PORTS='foreign'
        $psi=New-S1InstallProcessInfo fixture $arguments $v $paths
        if($psi.Environment.ContainsKey('VCPKG_OVERLAY_PORTS')){throw 'Inherited overlay leaked'}
    }finally{$env:VCPKG_OVERLAY_PORTS=$saved}
}
Case 'synthetic receipt accepted without ABI approval' '' {
    $v=New-Payload accepted
    Seal-Payload $v
    $r=Test-BR1Installed $v $plan
    if($r.abiVerified -or $r.binaryIdentityVerified -or $r.binaryReleaseApproved){throw 'Fixture promoted to approval'}
}
Case 'Brotli 1.2.0 status rejected' 'BR1Version' {
    $v=New-Payload stale
    $file=Join-Path (Get-BR1Paths $v $plan).installed 'vcpkg/status'
    [IO.File]::WriteAllText($file,[IO.File]::ReadAllText($file).Replace('Version: 1.2.1','Version: 1.2.0'),$utf8)
    Test-BR1Payload $v $plan
}
Case 'extra runtime DLL rejected' 'InputSet' {
    $v=New-Payload dll
    [IO.File]::WriteAllText((Join-Path (Get-BR1Paths $v $plan).installed 'x64-windows-shipping-privacy/bin/foreign.dll'),'foreign',$utf8)
    Test-BR1Payload $v $plan
}
Case 'installed byte tampering rejected' 'RawIdentity' {
    $v=New-Payload mutated
    Seal-Payload $v
    [IO.File]::AppendAllText((Join-Path (Get-BR1Paths $v $plan).installed 'x64-windows-shipping-privacy/bin/brotlidec.dll'),'mutation',$utf8)
    Test-BR1Installed $v $plan
}
Case 'unrecorded installed file rejected' 'InputSet' {
    $v=New-Payload extra-installed
    Seal-Payload $v
    [IO.File]::WriteAllText((Join-Path (Get-BR1Paths $v $plan).installed 'foreign.txt'),'foreign',$utf8)
    Test-BR1Installed $v $plan
}
Case 'legacy marker cannot admit SEC-BR1' 'S1:Fields' {
    $v=New-Payload old-marker
    $paths=Get-BR1Paths $v $plan
    Write-S1NewJson (Join-Path $paths.installed $script:BR1Marker) (Get-S1MarkerExpected $legacy)
    Test-BR1Installed $v $plan
}
Case 'boolean marker schema rejected' 'BR1Marker' {
    $v=New-Payload marker-type
    Seal-Payload $v
    $file=Join-Path (Get-BR1Paths $v $plan).installed $script:BR1Marker
    [IO.File]::WriteAllText($file,[IO.File]::ReadAllText($file).Replace('"schemaVersion": 1','"schemaVersion": true'),$utf8)
    Test-BR1Installed $v $plan
}
Case 'string install exit code rejected' 'BR1Receipt' {
    $v=New-Payload receipt-type
    Seal-Payload $v
    $file=Join-Path (Get-BR1Paths $v $plan).installed $script:BR1Receipt
    [IO.File]::WriteAllText($file,[IO.File]::ReadAllText($file).Replace('"installExitCode": 0','"installExitCode": "0"'),$utf8)
    Test-BR1Installed $v $plan
}
Case 'command failure preserves partial roots without success marker' '' {
    $v=New-View failed-command
    # Explicit test doubles: no source checkout, Git, bootstrap or compiler runs.
    function Test-S1ManagedOriginal($Root,$Original) {}
    function Test-S1ManagedDerivative($Root,$Derived) {}
    function Invoke-S1InstallCommand($Step,$Executable,$Arguments,$WorkingDirectory,$Paths){throw 'fixture command failure'}
    $errorText=''
    try{Invoke-BR1Install $v -BuildToolsEligibilityConfirmed}catch{$errorText=$_.Exception.Message}
    $paths=Get-BR1Paths $v $plan
    if(!$errorText.Contains('SEC-BR1:InstallFailed:git-init') -or ![IO.Directory]::Exists($paths.installed) -or ![IO.Directory]::Exists($paths.vcpkg)){throw 'Partial output not preserved'}
    foreach($relative in @($script:BR1Marker,$script:BR1Receipt)){
        if([IO.File]::Exists((Join-Path $paths.installed $relative))){throw 'Failed command published success evidence'}
    }
}
Case '19 installed DLLs permit exactly 17 deployed DLLs' '' {
    $v=New-Payload product
    Seal-Payload $v
    New-Product $v
    Test-BR1Product $v $plan
}
Case 'auxiliary WebP DLL is not part of product closure' 'InputSet' {
    $v=New-Payload product-extra
    Seal-Payload $v
    New-Product $v
    [IO.File]::WriteAllText((Join-Path (Get-BR1Paths $v $plan).product libwebpdemux.dll),'unexpected',$utf8)
    Test-BR1Product $v $plan
}
Case 'deployed Brotli bytes must match installed receipt' 'RawIdentity' {
    $v=New-Payload product-mutation
    Seal-Payload $v
    New-Product $v
    [IO.File]::AppendAllText((Join-Path (Get-BR1Paths $v $plan).product brotlidec.dll),'mutation',$utf8)
    Test-BR1Product $v $plan
}
$result=[ordered]@{result=$(if(@($results|Where-Object result -CEQ FAIL).Count){'FAIL'}else{'PASS'});kind='offline synthetic contract fixtures';cases=$results;actualInstallPerformed=$false}
Write-S1NewJson (Join-Path $run results.json) $result
$result|ConvertTo-Json -Depth 6
if($result.result -ceq 'FAIL'){exit 1}
