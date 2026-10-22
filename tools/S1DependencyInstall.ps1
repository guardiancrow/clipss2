#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Loaded by ShippingProfileS1; no work occurs on import or in PlanOnly.
$script:S1MarkerPath='share/clipss2-source-profile-s1-v1.json'
$script:S1ReceiptPath='share/clipss2-s1-install-receipt-v1.json'

function Write-S1NewJson($Path,$Value) {
    Assert-S1NoReparse $Path
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 12)+"`n")
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
function New-S1OwnedDirectory($Path) {
    Assert-S1NoReparse $Path
    # New-Item without Force refuses an existing directory, including empty ones.
    $null=New-Item -ItemType Directory -Path $Path -ErrorAction Stop
}
function Assert-S1InstallRoots($Root,$Plan) {
    $paths=[ordered]@{}
    foreach($key in $script:S1OutputPolicy.Keys){
        $path=Resolve-S1Path $Root $Plan.inputs.outputPolicy[$key]
        $paths[$key]=$path
        if($key -ceq 'derived'){continue}
        # A receipt is evidence to inspect, never permission to resume/overwrite.
        if([IO.Directory]::Exists($path) -or [IO.File]::Exists($path)){Stop-S1 ('InstallRootExists:'+ $key)}
    }
    $paths['original']=Resolve-S1Path $Root 'build/managed-jpegli-src'
    return $paths
}
function Test-S1InstallManaged($Root,$Paths) {
    $null=Test-S1ManagedOriginal $Root $Paths.original
    $null=Test-S1ManagedDerivative $Root $Paths.derived
}
function New-S1InstallProcessInfo($Executable,$Arguments,$WorkingDirectory,$Paths) {
    $psi=[Diagnostics.ProcessStartInfo]::new($Executable)
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
    $psi.WorkingDirectory=$WorkingDirectory
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    # No inherited overlays, binary/asset caches, Git redirects or toolchain files.
    foreach($key in @($psi.Environment.Keys)){
        if($key -match '^(GIT_|VCPKG_|X_VCPKG_|CMAKE_)' -or $key -in @('CL','_CL_','LINK','_LINK_','CFLAGS','CXXFLAGS','LDFLAGS','CC','CXX')){$null=$psi.Environment.Remove($key)}
    }
    $psi.Environment['GIT_CONFIG_NOSYSTEM']='1'
    $psi.Environment['GIT_CONFIG_GLOBAL']='NUL'
    $psi.Environment['GIT_TERMINAL_PROMPT']='0'
    $psi.Environment['GIT_NO_REPLACE_OBJECTS']='1'
    $psi.Environment['VCPKG_ROOT']=$Paths.vcpkg
    $psi.Environment['VCPKG_DISABLE_METRICS']='1'
    $psi.Environment['VCPKG_BINARY_SOURCES']='clear'
    $psi.Environment['X_VCPKG_ASSET_SOURCES']='clear'
    $psi.Environment['VCPKG_DOWNLOADS']=Join-Path $Paths.dependencies downloads
    $psi.Environment['X_VCPKG_REGISTRIES_CACHE']=Join-Path $Paths.dependencies registries
    $psi.Environment['TEMP']=Join-Path $Paths.dependencies tmp
    $psi.Environment['TMP']=$psi.Environment['TEMP']
    foreach($arg in $Arguments){$psi.ArgumentList.Add($arg)}
    return $psi
}
function Invoke-S1InstallCommand($Step,$Executable,$Arguments,$WorkingDirectory,$Paths) {
    $psi=New-S1InstallProcessInfo $Executable $Arguments $WorkingDirectory $Paths
    # Stream to private local logs, avoiding full build logs in console/JSON.
    $process=$null;$stdout=$null;$stderr=$null
    try{
        $stdout=[IO.File]::Open((Join-Path $Paths.dependencies ('logs/'+$Step+'.out.log')),[IO.FileMode]::CreateNew)
        $stderr=[IO.File]::Open((Join-Path $Paths.dependencies ('logs/'+$Step+'.err.log')),[IO.FileMode]::CreateNew)
        $process=[Diagnostics.Process]::Start($psi)
        $outTask=$process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $errTask=$process.StandardError.BaseStream.CopyToAsync($stderr)
        $process.WaitForExit()
        $outTask.GetAwaiter().GetResult();$errTask.GetAwaiter().GetResult()
        if($process.ExitCode -ne 0){Stop-S1 ('InstallCommand:'+ $Step+':'+$process.ExitCode)}
    }catch{
        if($process -and !$process.HasExited){$process.Kill($true);$process.WaitForExit()}
        throw
    }finally{
        if($stdout){$stdout.Dispose()};if($stderr){$stderr.Dispose()};if($process){$process.Dispose()}
    }
}
function Get-S1VcpkgPortTree($Paths,$Name) {
    $psi=New-S1InstallProcessInfo 'git' @('--no-optional-locks','-C',$Paths.vcpkg,'rev-parse',('HEAD:ports/'+$Name)) $Paths.vcpkg $Paths
    $p=[Diagnostics.Process]::Start($psi)
    try{
        $out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();$p.WaitForExit()
        $text=$out.GetAwaiter().GetResult().Trim();$null=$err.GetAwaiter().GetResult()
        if($p.ExitCode -ne 0){Stop-S1 ('PortTree:'+ $Name)}
        return $text
    }finally{$p.Dispose()}
}
function Assert-S1VcpkgSource($Paths,$Plan) {
    Assert-S1Checkout $Paths.vcpkg $Plan.inputs.builtinBaseline
    # Verify all official port trees, including the origins of the three overlays.
    foreach($package in $Plan.inputs.packages){
        if((Get-S1VcpkgPortTree $Paths $package.name) -cne $package.portTree){Stop-S1 ('PortTree:'+ $package.name)}
    }
}
function Get-S1InstallArguments($Root,$Paths,$Plan) {
    return @('install',('--vcpkg-root='+$Paths.vcpkg),('--x-manifest-root='+$Root),
        ('--triplet='+$Plan.inputs.targetTriplet),('--host-triplet='+$Plan.inputs.hostTriplet),
        ('--overlay-ports='+(Resolve-S1Path $Root $Plan.inputs.overlayRoot)),
        ('--overlay-triplets='+(Resolve-S1Path $Root triplets)),
        ('--x-install-root='+$Paths.installed),
        ('--x-buildtrees-root='+(Join-Path $Paths.dependencies buildtrees)),
        ('--x-packages-root='+(Join-Path $Paths.dependencies packages)),
        ('--downloads-root='+(Join-Path $Paths.dependencies downloads)),
        '--binarysource=clear','--x-asset-sources=clear','--no-print-usage')
}
function Test-S1DependencyPayload($Installed,$Plan) {
    $files=Get-S1Files $Installed
    $lists=@($files.Keys|Where-Object {$_ -clike 'vcpkg/info/*.list'})
    if($lists.Count -ne 16){Stop-S1 PackageLists}
    foreach($package in $Plan.inputs.packages){
        $triplet=if($package.role -ceq 'target'){$Plan.inputs.targetTriplet}else{$Plan.inputs.hostTriplet}
        $matching=@($lists|Where-Object {$_ -clike ('vcpkg/info/'+$package.name+'_*_'+$triplet+'.list')})
        if($matching.Count -ne 1){Stop-S1 PackageLists}
        $count=0
        foreach($line in [IO.File]::ReadAllLines($files[$matching[0]])){
            if(!$line){continue}
            $relative=$line.TrimEnd('/')
            Assert-S1RelativePath $relative
            if(!$relative.StartsWith($triplet+'/',[StringComparison]::Ordinal) -and $relative -cne $triplet){Stop-S1 PackageListPath}
            $path=Resolve-S1Path $Installed $relative
            if($line.EndsWith('/')){if(![IO.Directory]::Exists($path)){Stop-S1 PackagePayload};continue}
            if(![IO.File]::Exists($path) -or [IO.FileInfo]::new($path).Length -eq 0){Stop-S1 PackagePayload}
            $count++
        }
        if(!$count){Stop-S1 PackagePayload}
    }
    $target=$Plan.inputs.targetTriplet
    foreach($pattern in @("$target/bin/*.dll","$target/lib/*.lib","$target/include/*",'x64-windows/tools/pkgconf/*')){
        if(!@($files.Keys|Where-Object {$_ -clike $pattern}).Count){Stop-S1 PackagePayload}
    }
}
function Get-S1InstalledInventory($Installed) {
    $files=Get-S1Files $Installed
    return @(foreach($relative in ($files.Keys|Sort-Object -CaseSensitive)){
        if($relative -cin @($script:S1MarkerPath,$script:S1ReceiptPath)){continue}
        $bytes=[IO.File]::ReadAllBytes($files[$relative])
        [ordered]@{path=$relative;size=$bytes.Length;sha256=(Get-S1Hash $bytes)}
    })
}
function New-S1InstallReceipt($Installed,$Plan,$ToolSha256) {
    return [ordered]@{schemaVersion=1;receiptId='clipss2-s1-install-receipt-v1';
        sourceInputsSha256=$Plan.sourceInputsSha256;vcpkgBaseline=$Plan.inputs.builtinBaseline;
        vcpkgToolSha256=$ToolSha256;installExitCode=0;inventory=@(Get-S1InstalledInventory $Installed)}
}
function Test-S1InstallReceipt($Installed,$Plan) {
    $receipt=Read-S1StrictJson (Resolve-S1Path $Installed $script:S1ReceiptPath)
    Assert-S1Fields $receipt @('schemaVersion','receiptId','sourceInputsSha256','vcpkgBaseline','vcpkgToolSha256','installExitCode','inventory')
    if($receipt.schemaVersion -isnot [long] -and $receipt.schemaVersion -isnot [int]){Stop-S1 InstallReceipt}
    if($receipt.schemaVersion -ne 1 -or ($receipt.installExitCode -isnot [long] -and $receipt.installExitCode -isnot [int]) -or $receipt.installExitCode -ne 0){Stop-S1 InstallReceipt}
    Assert-S1Value $receipt.receiptId 'clipss2-s1-install-receipt-v1' InstallReceipt
    Assert-S1Value $receipt.sourceInputsSha256 $Plan.sourceInputsSha256 InstallReceipt
    Assert-S1Value $receipt.vcpkgBaseline $Plan.inputs.builtinBaseline InstallReceipt
    Assert-S1String $receipt.vcpkgToolSha256
    if($receipt.vcpkgToolSha256 -cnotmatch '^[A-F0-9]{64}$' -or $receipt.inventory -isnot [array] -or !$receipt.inventory.Count){Stop-S1 InstallReceipt}
    foreach($item in $receipt.inventory){
        Assert-S1Fields $item @('path','size','sha256');Assert-S1RelativePath $item.path
        Assert-S1Identity (Resolve-S1Path $Installed $item.path) $item.size $item.sha256
    }
    $actual=@(Get-S1InstalledInventory $Installed)
    Assert-S1InputSet @($actual.path) @($receipt.inventory.path)
    Test-S1DependencyPayload $Installed $Plan
    # Self-recorded hashes detect corruption, not provenance/ABI/rights approval.
    return [pscustomobject]@{inventoryVerified=$true;abiVerified=$false;binaryIdentityVerified=$false}
}
function Invoke-S1DependencyInstall($Root,[switch]$BuildToolsEligibilityConfirmed) {
    if(!$BuildToolsEligibilityConfirmed){Stop-S1 BuildToolsEligibilityUnconfirmed}
    if(![OperatingSystem]::IsWindows() -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::X64){Stop-S1 InstallHost}
    $plan=Read-S1SourceInputs $Root
    $paths=Assert-S1InstallRoots $Root $plan
    Test-S1InstallManaged $Root $paths
    $git=(Get-Command git -CommandType Application -ErrorAction Stop).Source
    # All validation above is read-only. Fresh roots only; no automatic cleanup.
    New-S1OwnedDirectory $paths.dependencies
    Write-S1NewJson (Resolve-S1Path $paths.dependencies owner.json) ([ordered]@{
        schemaVersion=1;ownerId='clipss2-s1-dependency-install-v1';sourceInputsSha256=$plan.sourceInputsSha256;
        state='UNADMITTED';reuseAllowed=$false})
    foreach($dir in @('logs','tmp','downloads','registries','buildtrees','packages')){New-S1OwnedDirectory (Join-Path $paths.dependencies $dir)}
    New-S1OwnedDirectory $paths.vcpkg
    New-S1OwnedDirectory $paths.installed
    $step='git-init'
    try{
        Invoke-S1InstallCommand $step $git @('-c','init.templateDir=','init',$paths.vcpkg) $Root $paths
        $step='git-fetch'
        Invoke-S1InstallCommand $step $git @('-C',$paths.vcpkg,'-c','protocol.file.allow=never','-c','protocol.ext.allow=never','fetch','--depth=1','https://github.com/microsoft/vcpkg.git',$plan.inputs.builtinBaseline) $Root $paths
        $step='git-checkout'
        Invoke-S1InstallCommand $step $git @('-C',$paths.vcpkg,'-c','core.autocrlf=false','checkout','--detach',$plan.inputs.builtinBaseline) $Root $paths
        $step='vcpkg-source'
        Assert-S1VcpkgSource $paths $plan
        $step='bootstrap'
        Invoke-S1InstallCommand $step (Join-Path $PSHOME pwsh.exe) @('-NoProfile','-File',(Join-Path $paths.vcpkg scripts/bootstrap.ps1),'-disableMetrics') $paths.vcpkg $paths
        $tool=Resolve-S1Path $paths.vcpkg vcpkg.exe
        if(![IO.File]::Exists($tool)){Stop-S1 VcpkgToolMissing}
        $toolSha=Get-S1Hash ([IO.File]::ReadAllBytes($tool))
        $step='install'
        Invoke-S1InstallCommand $step $tool (Get-S1InstallArguments $Root $paths $plan) $Root $paths
        $step='acceptance'
        # Recheck inputs, checkout, original/derived bytes after external work.
        $plan=Read-S1SourceInputs $Root
        Assert-S1VcpkgSource $paths $plan
        Test-S1InstallManaged $Root $paths
        if((Get-S1Hash ([IO.File]::ReadAllBytes($tool))) -cne $toolSha){Stop-S1 VcpkgToolChanged}
        $null=Test-S1InstalledStructure $Root $paths.installed $plan
        Test-S1DependencyPayload $paths.installed $plan
        foreach($relative in @($script:S1MarkerPath,$script:S1ReceiptPath)){
            $file=Resolve-S1Path $paths.installed $relative
            if([IO.File]::Exists($file) -or [IO.Directory]::Exists($file)){Stop-S1 PrematureInstallMarker}
        }
        [void][IO.Directory]::CreateDirectory((Join-Path $paths.installed share))
        Write-S1NewJson (Resolve-S1Path $paths.installed $script:S1ReceiptPath) (New-S1InstallReceipt $paths.installed $plan $toolSha)
        $null=Test-S1InstallReceipt $paths.installed $plan
        # Success marker is the last write, after command success and acceptance.
        $pending=Resolve-S1Path $paths.dependencies install-marker.pending.json
        Write-S1NewJson $pending (Get-S1MarkerExpected $plan)
        # Same-volume atomic publication: failed/partial JSON writes stay outside
        # installed. Move refuses an existing marker and never replaces it.
        [IO.File]::Move($pending,(Resolve-S1Path $paths.installed $script:S1MarkerPath),$false)
    }catch{
        # Preserve partial output and logs; no marker publication on earlier failure.
        throw [IO.InvalidDataException]::new(('S1:InstallFailed:'+ $step+'; partial roots preserved; '+$_.Exception.Message),$_.Exception)
    }
    return [ordered]@{result='PASS';mode='Install';actualInstallPerformed=$true;installedValidated=$true;
        packages=16;boundInputs=28;markerPublished=$true;abiVerified=$false;binaryIdentityVerified=$false;
        productBuild=$false;binaryReleaseApproved=$false}
}
