#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Offline tests. External install/bootstrap/Git commands are transcript fixtures.
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=(Resolve-Path "$PSScriptRoot/..").Path
. "$source/tools/ShippingProfileS1.ps1"
Assert-S1NoReparse $EvidenceRoot
$run=Join-Path $EvidenceRoot ('install-fixtures-'+[guid]::NewGuid().ToString('N'))
New-S1OwnedDirectory $run
$utf8=[Text.UTF8Encoding]::new($false)
$results=[Collections.Generic.List[object]]::new()
$ledger=Read-S1SourceInputs $source
function New-View($Name){
    $view=Join-Path $run $Name
    $paths=@($ledger.inputs.files.path)+@('cmake/privacy/source-inputs-s1-v1.json',
        'tools/S1SourceContracts.psm1','tools/ShippingPrivacyS1.ps1','tools/ShippingProfileS1.ps1',
        'tools/S1DependencyInstall.ps1','tools/build_shipping_dependencies_s1.ps1')
    foreach($relative in $paths){
        $dest=Join-Path $view $relative
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        [IO.File]::Copy((Join-Path $source $relative),$dest,$false)
    }
    return $view
}
function Snapshot($View){
    $items=@(Get-ChildItem -LiteralPath $View -Force -Recurse | Sort-Object FullName | ForEach-Object {
        $rel=[IO.Path]::GetRelativePath($View,$_.FullName)
        if($_.PSIsContainer){'D '+$rel}else{'F '+$rel+' '+(Get-FileHash -LiteralPath $_.FullName).Hash}
    })
    return ($items -join "`n")
}
function Case($Id,$Expected,$Body){
    $actual='ACCEPT';$detail=$null
    try{& $Body|Out-Null}catch{$actual=$_.Exception.Message;$detail=$_.ScriptStackTrace}
    $ok=if(!$Expected){$actual -ceq 'ACCEPT'}else{$actual -clike ('*S1:'+$Expected+'*')}
    $results.Add([ordered]@{id=$Id;result=$(if($ok){'PASS'}else{'FAIL'});actual=$actual;detail=$(if(!$ok){$detail}else{$null})})
}
function New-Payload($Installed,$Plan){
    foreach($relative in @('share','vcpkg/info','x64-windows-shipping-privacy/bin','x64-windows-shipping-privacy/lib',
        'x64-windows-shipping-privacy/include','x64-windows-shipping-privacy/share','x64-windows/tools','x64-windows/share')){
        [void][IO.Directory]::CreateDirectory((Join-Path $Installed $relative))
    }
    $status=@(foreach($package in $Plan.inputs.packages){
        $triplet=if($package.role -ceq 'target'){$Plan.inputs.targetTriplet}else{$Plan.inputs.hostTriplet}
        $version=$package.version.Split('#');$port=if($version.Count -gt 1){$version[1]}else{'0'}
        $payload="$triplet/share/$($package.name)/fixture.txt"
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName("$Installed/$payload"))
        [IO.File]::WriteAllText("$Installed/$payload",'synthetic payload',$utf8)
        [IO.File]::WriteAllText("$Installed/vcpkg/info/$($package.name)_$($package.version)_$triplet.list",($payload+"`n"),$utf8)
        "Package: $($package.name)`nVersion: $($version[0])`nPort-Version: $port`nArchitecture: $triplet`nDescription: fixture`n continuation`nStatus: install ok installed"
    })
    [IO.File]::WriteAllText("$Installed/vcpkg/status",(($status -join "`n`n")+"`n"),$utf8)
    foreach($relative in @('x64-windows-shipping-privacy/bin/fixture.dll','x64-windows-shipping-privacy/lib/fixture.lib',
        'x64-windows-shipping-privacy/include/fixture.h','x64-windows/tools/pkgconf/pkgconf.exe')){
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName("$Installed/$relative"))
        [IO.File]::WriteAllText("$Installed/$relative",'fixture only; never execute',$utf8)
    }
}
function Invoke-FixtureInstall($View,$FailStep='',$BadPayload=$false,$TamperOverlayAt=''){
    $calls=[Collections.Generic.List[string]]::new()
    $managedCalls=[Collections.Generic.List[string]]::new()
    function Test-S1InstallManaged($Root,$Paths){$managedCalls.Add('verify-original-and-derived')}
    function Assert-S1VcpkgSource($Paths,$Plan,$Root){
        if($Root -cne $View){throw 'Wrong source input root'}
        $calls.Add('verify-baseline-and-port-trees')
        if($FailStep -ceq 'vcpkg-source'){Stop-S1 FixtureSourceValidation}
    }
    function Invoke-S1InstallCommand($Step,$Executable,$Arguments,$WorkingDirectory,$Paths){
        $calls.Add($Step)
        if([IO.File]::Exists((Join-Path $Paths.installed $script:S1MarkerPath))){throw 'Premature marker'}
        if($Step -ceq $FailStep){Stop-S1 ('FixtureCommand:'+ $Step)}
        if($Step -ceq 'bootstrap'){[IO.File]::WriteAllText((Join-Path $Paths.vcpkg vcpkg.exe),'not executable: fixture',$utf8)}
        if($Step -ceq 'install'){
            New-Payload $Paths.installed $ledger
            if($BadPayload){[IO.File]::WriteAllText((Join-Path $Paths.installed vcpkg/status),'broken',$utf8)}
        }
        if($Step -ceq $TamperOverlayAt){[IO.File]::AppendAllText("$View/vcpkg-overlays/shipping-privacy-v2/libjxl/portfile.cmake",' changed during external work',$utf8)}
    }
    $result=Invoke-S1DependencyInstall $View -BuildToolsEligibilityConfirmed
    if($calls -join ',' -cne 'git-init,git-fetch,git-checkout,verify-baseline-and-port-trees,bootstrap,install,verify-baseline-and-port-trees'){throw 'Command order'}
    if($managedCalls.Count -ne 2){throw 'Managed bytes not checked before and after'}
    return $result
}

$view=New-View plan
Case 'plan.fixed-inputs-and-readonly' '' {
    $before=Snapshot $view
    $p=Get-S1Plan $view v2 s1-v1 ON Release x64
    if($p.boundInputs -ne 28 -or $p.packages.Count -ne 16 -or $p.sideEffects -cne 'NONE' -or $p.vcpkgInvoked -or $p.compilerProbe -or $p.actualInstallPerformed){throw 'Plan contract'}
    if((Snapshot $view) -cne $before){throw 'Plan mutated view'}
}
Case 'install.eligibility-gate-no-writes' 'BuildToolsEligibilityUnconfirmed' {
    $before=Snapshot $view
    try{Invoke-S1DependencyInstall $view}finally{if((Snapshot $view) -cne $before){throw 'Gate wrote files'}}
}
Case 'install.missing-managed-no-writes' 'RootMissing' {
    $before=Snapshot $view
    try{Invoke-S1DependencyInstall $view -BuildToolsEligibilityConfirmed}finally{if((Snapshot $view) -cne $before){throw 'Managed gate wrote files'}}
}
foreach($key in @('vcpkg','dependencies','installed','product')){
    $v=New-View ('existing-'+$key)
    $path=Resolve-S1Path $v $ledger.inputs.outputPolicy[$key]
    [void][IO.Directory]::CreateDirectory($path)
    [IO.File]::WriteAllText((Join-Path $path sentinel),'unknown existing data',$utf8)
    Case ('roots.refuse-and-preserve.'+$key) ('InstallRootExists:'+$key) {
        $before=Snapshot $v
        try{Assert-S1InstallRoots $v $ledger}finally{if((Snapshot $v) -cne $before){throw 'Existing data changed'}}
    }
}
Case 'roots.file-instead-of-directory' 'InstallRootExists:installed' {
    $v=New-View file-root;[void][IO.Directory]::CreateDirectory("$v/build")
    [IO.File]::WriteAllText("$v/build/vcpkg_installed-s1-v2",'sentinel',$utf8)
    Assert-S1InstallRoots $v $ledger
}
Case 'roots.junction-rejected' 'ReparsePoint' {
    $v=New-View junction-root;[void][IO.Directory]::CreateDirectory("$v/build")
    $null=New-Item -ItemType Junction -Path "$v/build/shipping-dependencies-s1-v2" -Target $view
    Assert-S1InstallRoots $v $ledger
}
Case 'roots.atomic-create-refuses-existing' '' {
    $path=Join-Path $run atomic;New-S1OwnedDirectory $path
    $refused=$false;try{New-S1OwnedDirectory $path}catch{$refused=$true}
    if(!$refused){throw 'Reused existing directory'}
}
Case 'inputs.tampered-triplet' 'RawIdentity' {
    $v=New-View tampered;[IO.File]::AppendAllText("$v/triplets/x64-windows-shipping-privacy.cmake",' ',$utf8)
    Read-S1SourceInputs $v
}
Case 'managed.empty-derived-refused' 'InputSet' {
    $v=New-View empty-derived;[void][IO.Directory]::CreateDirectory("$v/build/managed-jpegli-s1-v2")
    Test-S1ManagedDerivative $v "$v/build/managed-jpegli-s1-v2"
}
Case 'install.arguments-dedicated-roots-no-cache' '' {
    $paths=Assert-S1InstallRoots $view $ledger
    $a=Get-S1InstallArguments $view $paths $ledger
    foreach($wanted in @('--binarysource=clear','--x-asset-sources=clear','--triplet=x64-windows-shipping-privacy','--host-triplet=x64-windows',('--x-install-root='+$paths.installed))){if($wanted -cnotin $a){throw 'Missing isolation flag'}}
    if(@($a|Where-Object {$_ -cmatch '^--(clean|editable|head|keep-going|only-binarycaching)'}).Count){throw 'Unsafe option'}
    $psi=New-S1InstallProcessInfo fixture $a $view $paths
    if($psi.UseShellExecute -or $psi.Environment['VCPKG_BINARY_SOURCES'] -cne 'clear' -or $psi.Environment['VCPKG_DOWNLOADS'] -cne (Join-Path $paths.dependencies downloads)){throw 'Environment isolation'}
}
Case 'vcpkg.baseline-registry-trees-and-effective-overlays' '' {
    $paths=Assert-S1InstallRoots $view $ledger
    $checked=[Collections.Generic.List[string]]::new()
    function Assert-S1Checkout($Root,$Revision){if($Root -cne $paths.vcpkg -or $Revision -cne $ledger.inputs.builtinBaseline){throw 'Wrong checkout validation'};$checked.Add('baseline')}
    function Get-S1VcpkgPortTree($Paths,$Name){
        if($Name -cin @('highway','libjxl','libyuv')){throw 'Overlay incorrectly compared to registry'}
        $checked.Add($Name);return @($ledger.inputs.packages|Where-Object name -CEQ $Name)[0].portTree
    }
    $before=Snapshot $view
    Assert-S1VcpkgSource $paths $ledger $view
    if($checked.Count -ne 14){throw 'Missing baseline or registry port validation'}
    if((Snapshot $view) -cne $before){throw 'Source acceptance wrote files'}
}
foreach($registryName in @($ledger.inputs.packages.name|Where-Object {$_ -cnotin @('highway','libjxl','libyuv')})){
    Case ('vcpkg.registry-tree-mismatch.'+$registryName) ('PortTree:'+$registryName) {
        $paths=Assert-S1InstallRoots $view $ledger
        function Assert-S1Checkout($Root,$Revision){}
        function Get-S1VcpkgPortTree($Paths,$Name){
            if($Name -ceq $registryName){return ('0'*40)}
            return @($ledger.inputs.packages|Where-Object name -CEQ $Name)[0].portTree
        }
        Assert-S1VcpkgSource $paths $ledger $view
    }
}
foreach($overlayName in @('highway','libjxl','libyuv')){
    foreach($mutation in @('name','version','port-version','portfile','patch')){
        Case ('vcpkg.overlay-rejects.'+$overlayName+'.'+$mutation) 'RawIdentity' {
            $v=New-View ('overlay-'+$overlayName+'-'+$mutation)
            $port="$v/vcpkg-overlays/shipping-privacy-v2/$overlayName"
            $file="$port/vcpkg.json"
            switch($mutation){
                name {[IO.File]::WriteAllText($file,([IO.File]::ReadAllText($file).Replace(('"name": "'+$overlayName+'"'),'"name": "unexpected"')),$utf8)}
                version {[IO.File]::WriteAllText($file,([IO.File]::ReadAllText($file) -creplace '("version(?:-semver)?": ")[^"]+','${1}0.0.0'),$utf8)}
                port-version {[IO.File]::WriteAllText($file,([IO.File]::ReadAllText($file).Replace('{','{"port-version": 1,')),$utf8)}
                portfile {[IO.File]::AppendAllText("$port/portfile.cmake",' changed',$utf8)}
                patch {
                    $file=@($ledger.inputs.files.path|Where-Object {$_ -clike ("*/$overlayName/*.patch") -or $_ -clike ("*/$overlayName/*.diff")})[0]
                    [IO.File]::AppendAllText((Join-Path $v $file),' changed',$utf8)
                }
            }
            Assert-S1VcpkgSource (Assert-S1InstallRoots $v $ledger) $ledger $v
        }
    }
}
foreach($extra in @('CONTROL','vcpkg.json','foreign/vcpkg.json')){
    Case ('vcpkg.overlay-rejects-extra.'+$extra) 'InputSet' {
        $v=New-View ('extra-overlay-'+$extra.Replace('/','-'))
        $file="$v/vcpkg-overlays/shipping-privacy-v2/$extra"
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))
        [IO.File]::WriteAllText($file,'{}',$utf8)
        Assert-S1VcpkgSource (Assert-S1InstallRoots $v $ledger) $ledger $v
    }
}
Case 'vcpkg.overlay-config-replacement-refused' 'RawIdentity' {
    $v=New-View overlay-config
    [IO.File]::WriteAllText("$v/vcpkg-configuration.json",'{"overlay-ports":["foreign"]}',$utf8)
    Get-S1InstallArguments $v (Assert-S1InstallRoots $v $ledger) $ledger
}
Case 'vcpkg.inherited-overlay-cannot-replace-or-add-ports' '' {
    $savedPorts=[Environment]::GetEnvironmentVariable('VCPKG_OVERLAY_PORTS')
    $savedTriplets=[Environment]::GetEnvironmentVariable('VCPKG_OVERLAY_TRIPLETS')
    try{
        $env:VCPKG_OVERLAY_PORTS=Join-Path $run foreign
        $env:VCPKG_OVERLAY_TRIPLETS=Join-Path $run foreign-triplets
        $paths=Assert-S1InstallRoots $view $ledger
        $a=Get-S1InstallArguments $view $paths $ledger
        $psi=New-S1InstallProcessInfo fixture $a $view $paths
        if($psi.Environment.ContainsKey('VCPKG_OVERLAY_PORTS') -or $psi.Environment.ContainsKey('VCPKG_OVERLAY_TRIPLETS')){throw 'Inherited overlay accepted'}
        $selected=@($psi.ArgumentList|Where-Object {$_ -clike '--overlay-ports=*'})
        if($selected.Count -ne 1 -or $selected[0] -cne ('--overlay-ports='+(Resolve-S1Path $view $ledger.inputs.overlayRoot))){throw 'Wrong effective overlay'}
    }finally{
        [Environment]::SetEnvironmentVariable('VCPKG_OVERLAY_PORTS',$savedPorts)
        [Environment]::SetEnvironmentVariable('VCPKG_OVERLAY_TRIPLETS',$savedTriplets)
    }
}
Case 'managed.fixed-roots-forwarded' '' {
    $paths=Assert-S1InstallRoots $view $ledger
    $seen=[Collections.Generic.List[string]]::new()
    function Test-S1ManagedOriginal($Root,$Original){if($Original -cne "$view\build\managed-jpegli-src"){throw 'Original override'};$seen.Add('original')}
    function Test-S1ManagedDerivative($Root,$Derived){if($Derived -cne "$view\build\managed-jpegli-s1-v2"){throw 'Derived override'};$seen.Add('derived')}
    Test-S1InstallManaged $view $paths
    if(($seen -join ',') -cne 'original,derived'){throw 'Wrong managed order'}
}
Case 'process.failure-preserves-logs' 'InstallCommand:fixture:19' {
    $v=New-View native-runner
    $paths=Assert-S1InstallRoots $v $ledger
    foreach($dir in @('logs','tmp')){[void][IO.Directory]::CreateDirectory((Join-Path $paths.dependencies $dir))}
    $worker=Join-Path $paths.dependencies fixture.ps1
    [IO.File]::WriteAllText($worker,"[Console]::Out.WriteLine('fixture stdout'); [Console]::Error.WriteLine('fixture stderr'); exit 19",$utf8)
    try{Invoke-S1InstallCommand fixture (Join-Path $PSHOME pwsh.exe) @('-NoProfile','-File',$worker) $v $paths}finally{
        if([IO.File]::ReadAllText((Join-Path $paths.dependencies logs/fixture.out.log)).Trim() -cne 'fixture stdout'){throw 'stdout missing'}
        if([IO.File]::ReadAllText((Join-Path $paths.dependencies logs/fixture.err.log)).Trim() -cne 'fixture stderr'){throw 'stderr missing'}
    }
}
foreach($step in @('git-init','git-fetch','git-checkout','vcpkg-source','bootstrap','install')){
    Case ('install.failure-no-marker.'+$step) ('InstallFailed:'+ $step) {
        $v=New-View ('failed-'+$step)
        try{Invoke-FixtureInstall $v $step}finally{
            if([IO.File]::Exists("$v/build/vcpkg_installed-s1-v2/$script:S1MarkerPath")){throw 'Failed install admitted'}
            if(![IO.File]::Exists("$v/build/shipping-dependencies-s1-v2/owner.json")){throw 'Partial roots missing'}
        }
    }
}
foreach($tamperAt in @('bootstrap','install')){
    $failedStage=if($tamperAt -ceq 'bootstrap'){'install'}else{'acceptance'}
    Case ('install.changed-overlay-no-marker.'+$tamperAt) ('InstallFailed:'+$failedStage) {
        $v=New-View ('changed-overlay-at-'+$tamperAt)
        try{Invoke-FixtureInstall $v '' $false $tamperAt}finally{
            foreach($relative in @($script:S1MarkerPath,$script:S1ReceiptPath)){
                if([IO.File]::Exists("$v/build/vcpkg_installed-s1-v2/$relative")){throw 'Changed overlay admitted'}
            }
        }
    }
}
Case 'install.zero-exit-invalid-payload-no-marker' 'InstallFailed:acceptance' {
    $v=New-View bad-payload
    try{Invoke-FixtureInstall $v '' $true}finally{if([IO.File]::Exists("$v/build/vcpkg_installed-s1-v2/$script:S1MarkerPath")){throw 'Invalid payload admitted'}}
}
$accepted=New-View accepted
Case 'install.fixture-order-and-last-write-marker' '' {
    $r=Invoke-FixtureInstall $accepted
    if(!$r.markerPublished -or $r.abiVerified -or $r.binaryIdentityVerified -or $r.productBuild -or $r.binaryReleaseApproved){throw 'False acceptance claim'}
    $validation=Test-S1Installed $accepted "$accepted/build/vcpkg_installed-s1-v2" $ledger
    if(!$validation.markerValidated -or $validation.actualInstallPerformed -or $validation.abiVerified -or $validation.binaryIdentityVerified){throw 'False validation claim'}
}
Case 'install.completed-root-cannot-be-reinstalled' 'InstallRootExists:vcpkg' {Invoke-FixtureInstall $accepted}
Case 'marker.write-cannot-overwrite' '' {
    $path="$accepted/build/vcpkg_installed-s1-v2/$script:S1MarkerPath";$before=[IO.File]::ReadAllText($path)
    $refused=$false;try{Write-S1NewJson $path @{invalid=1}}catch{$refused=$true}
    if(!$refused -or [IO.File]::ReadAllText($path) -cne $before){throw 'Marker overwritten'}
}
Case 'marker.alone-not-admitted' 'FileMissing' {
    $v=New-View marker-only;$installed="$v/build/vcpkg_installed-s1-v2"
    New-Payload $installed $ledger
    Write-S1NewJson "$installed/$script:S1MarkerPath" (Get-S1MarkerExpected $ledger)
    Test-S1Installed $v $installed $ledger
}
Case 'marker.old-id-refused' 'MarkerIdentity' {
    $v=New-View old-marker-id;Invoke-FixtureInstall $v
    $i="$v/build/vcpkg_installed-s1-v2"
    $text=[IO.File]::ReadAllText("$i/$script:S1MarkerPath").Replace('clipss2-s1-installed-profile-v1','clipss2-shipping-profile-v2')
    [IO.File]::WriteAllText("$i/$script:S1MarkerPath",$text,$utf8)
    Test-S1Installed $v $i $ledger
}
Case 'receipt.nonzero-exit-refused' 'InstallReceipt' {
    $v=New-View nonzero-receipt;Invoke-FixtureInstall $v
    $i="$v/build/vcpkg_installed-s1-v2"
    $receipt=Read-S1StrictJson "$i/$script:S1ReceiptPath";$receipt.installExitCode=1
    [IO.File]::WriteAllText("$i/$script:S1ReceiptPath",($receipt|ConvertTo-Json -Depth 12),$utf8)
    Test-S1Installed $v $i $ledger
}
foreach($mutation in @('version','architecture','status')){
    Case ('status.rejects.'+$mutation) 'InstalledStatus' {
        $v=New-View ('bad-status-'+$mutation);Invoke-FixtureInstall $v
        $i="$v/build/vcpkg_installed-s1-v2"
        $text=[IO.File]::ReadAllText("$i/vcpkg/status")
        $text=switch($mutation){version{$text.Replace('Version: 3.15.0','Version: 0.0.0')};architecture{$text.Replace('Architecture: x64-windows-shipping-privacy','Architecture: x64-windows')};status{$text.Replace('Status: install ok installed','Status: install ok half-installed')}}
        [IO.File]::WriteAllText("$i/vcpkg/status",$text,$utf8)
        Test-S1Installed $v $i $ledger
    }
}
Case 'receipt.rejects-byte-change' 'RawIdentity' {
    [IO.File]::AppendAllText("$accepted/build/vcpkg_installed-s1-v2/x64-windows-shipping-privacy/bin/fixture.dll",' changed',$utf8)
    Test-S1Installed $accepted "$accepted/build/vcpkg_installed-s1-v2" $ledger
}
Case 'receipt.rejects-extra-file' 'InputSet' {
    $v=New-View extra-file;Invoke-FixtureInstall $v
    [IO.File]::WriteAllText("$v/build/vcpkg_installed-s1-v2/extra",'unlisted',$utf8)
    Test-S1Installed $v "$v/build/vcpkg_installed-s1-v2" $ledger
}
Case 'payload.rejects-traversal' 'RelativePath' {
    $v=New-View traversal;$i="$v/build/vcpkg_installed-s1-v2";New-Payload $i $ledger
    $list=@(Get-ChildItem "$i/vcpkg/info/*.list")[0].FullName
    [IO.File]::WriteAllText($list,"../outside`n",$utf8)
    Test-S1DependencyPayload $i $ledger
}
Case 'payload.rejects-empty-list' 'PackagePayload' {
    $v=New-View empty-list;$i="$v/build/vcpkg_installed-s1-v2";New-Payload $i $ledger
    $list=@(Get-ChildItem "$i/vcpkg/info/*.list")[0].FullName
    [IO.File]::WriteAllText($list,'',$utf8)
    Test-S1DependencyPayload $i $ledger
}
$failed=@($results|Where-Object result -CEQ FAIL)
$summary=[ordered]@{result=$(if($failed.Count){'FAIL'}else{'PASS'});cases=$results.Count;failed=$failed.Count;
    actualVcpkgInstall=$false;msvcCompilation=$false;shippingConfigure=$false;productBuild=$false;
    boundary='Offline metadata/filesystem and explicit command/managed-validator fixtures only; no toolchain eligibility or real install acceptance.';
    results=@($results)}
Write-S1NewJson (Join-Path $run results.json) $summary
[ordered]@{result=$summary.result;cases=$results.Count;failed=$failed.Count;failures=$failed;receipt=(Join-Path $run results.json)}|ConvertTo-Json -Depth 8
if($failed.Count){exit 1}
