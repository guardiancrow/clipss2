#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# New candidate admission. Historical validators are used only on the ORIGINAL dependency checkout.
. (Join-Path $PSScriptRoot ShippingProfileSecBR2.ps1)

function Get-IconHash($Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Write-IconJson($Path, $Value) {
    $json = (ConvertTo-Json -InputObject $Value -Depth 30).Replace("`r`n","`n")
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json + "`n")
    Assert-S1NoReparse $Path
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes); $stream.Flush($true) } finally { $stream.Dispose() }
}
function Get-IconBoundPaths($Root) {
    $paths = @('CMakeLists.txt','cmake/IconRelease.cmake','cmake/ManagedJpegli.cmake',
        'cmake/LibjxlPackage.cmake','cmake/LibjxlSmoke.cmake',
        'tools/IconV012.psm1','tools/build_icon_v012.ps1','tools/verify_app_icon.ps1',
        'tools/ShippingProfileSecBR2.ps1','tools/ShippingProfileSecBR1.ps1',
        'tools/ShippingProfileS1.ps1','tools/ShippingPrivacyS1.ps1','tools/S1SourceContracts.psm1',
        'tests/icon_v012_contract_test.ps1')
    $paths += @((Get-S1Files (Join-Path $Root src)).Keys | ForEach-Object { 'src/' + $_ })
    $paths += @((Get-S1Files (Join-Path $Root tests)).Keys | Where-Object { $_ -match '\.(cpp|h)$' } | ForEach-Object { 'tests/' + $_ })
    return @($paths | Sort-Object -Unique)
}
function Read-IconInputs($Root) {
    $file = Join-Path $Root 'cmake/icon-v012/source-inputs-v1.json'
    $inputs = Read-S1StrictJson $file
    if ($inputs.schemaVersion -ne 1 -or $inputs.inputId -cne 'clipss2-icon-v012-inputs-v1' -or
        $inputs.version -cne '0.1.2' -or $inputs.scope -cne 'icon-v012' -or
        $inputs.dependencyInputsSha256 -cne $script:BR1LedgerSha -or
        $inputs.installedReceiptSha256 -cne '0D84E144C82AA74208CF57B93271D1C33EF0724643E24C947E21338EFD617D35' -or
        $inputs.installedMarkerSha256 -cne '56116DE94EAC39F8B5D7179629ABE0E78C1C2A205542257688268F0239CC688D') { throw 'ICON-012:InputContract' }
    Assert-S1InputSet @(Get-IconBoundPaths $Root) @($inputs.files.path)
    foreach ($binding in $inputs.files) { Assert-S1Identity (Resolve-S1Path $Root $binding.path) $binding.size $binding.sha256 }
    return [pscustomobject]@{ inputs=$inputs; sha256=(Get-IconHash $file) }
}
function Get-IconGit($Root, $Arguments) {
    $value = & git -C $Root @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'ICON-012:Git' }
    return ($value -join "`n").Trim()
}
function Assert-IconBuildSource($Root, $ExpectedCommit) {
    $plan = Read-IconInputs $Root
    $head = Get-IconGit $Root @('rev-parse','HEAD')
    if ($ExpectedCommit -notmatch '^[0-9a-f]{40}$' -or $head -cne $ExpectedCommit) { throw 'ICON-012:BuildCommit' }
    if (Get-IconGit $Root @('status','--porcelain=v1','--untracked-files=all')) { throw 'ICON-012:SourceDirty' }
    return $plan
}
function Test-IconDependencies($DependencyRoot) {
    $original = (Resolve-Path -LiteralPath $DependencyRoot).Path
    $plan = Read-BR2SourceInputs $original
    $result = Test-BR2Dependencies $original $plan
    return $result
}
function Test-IconManaged($DependencyRoot) {
    $plan = Read-BR2SourceInputs $DependencyRoot
    $paths = Get-BR1Paths $DependencyRoot $plan.br1
    $null = Test-S1ManagedOriginal $DependencyRoot $paths.original
    return Test-S1ManagedDerivative $DependencyRoot $paths.derived
}
function Enter-IconDeveloperShell {
    if (!(Get-Command cl -ErrorAction SilentlyContinue)) {
        $finder = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $vs = & $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (!$vs -or $LASTEXITCODE -ne 0) { throw 'ICON-012:MSVCMissing' }
        Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
        Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    }
}
function Read-IconPE($Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($bytes, 60)
    if ([BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664) { throw 'ICON-012:PEArchitecture' }
    $count = [BitConverter]::ToUInt16($bytes,$pe+6)
    $optionalSize = [BitConverter]::ToUInt16($bytes,$pe+20)
    $optional = $pe + 24
    if ([BitConverter]::ToUInt16($bytes,$optional) -ne 0x20b) { throw 'ICON-012:PE32Plus' }
    $sections = $optional + $optionalSize
    $map = {
        param([uint32]$Rva)
        for ($section=0; $section -lt $count; ++$section) {
            $offset = $sections + $section * 40
            $virtual = [BitConverter]::ToUInt32($bytes,$offset+12)
            $size = [Math]::Max([BitConverter]::ToUInt32($bytes,$offset+8),[BitConverter]::ToUInt32($bytes,$offset+16))
            if ($Rva -ge $virtual -and $Rva -lt ($virtual+$size)) { return [int]([BitConverter]::ToUInt32($bytes,$offset+20)+$Rva-$virtual) }
        }
        throw 'ICON-012:ImportRva'
    }
    $imports = @()
    $importRva = [BitConverter]::ToUInt32($bytes,$optional+120)
    if ($importRva) {
        $descriptor = & $map $importRva
        while ([BitConverter]::ToUInt32($bytes,$descriptor+12)) {
            $name = & $map ([BitConverter]::ToUInt32($bytes,$descriptor+12))
            $end = $name
            while ($bytes[$end]) { ++$end }
            $imports += [Text.Encoding]::ASCII.GetString($bytes,$name,$end-$name)
            $descriptor += 20
        }
    }
    if (@($imports | Where-Object { $_ -match '^(msvcp\d+.*d|vcruntime\d+.*d|ucrtbased|concrt\d+d)\.dll$' }).Count) { throw 'ICON-012:DebugCRT' }
    foreach ($encoding in @([Text.Encoding]::Latin1,[Text.Encoding]::Unicode)) {
        if ($encoding.GetString($bytes) -match '(?i)[a-z]:[\\/]Users[\\/]|/home/[^\x00\s/]+/') { throw 'ICON-012:PrivateBinaryPath' }
    }
    return [ordered]@{path=[IO.Path]::GetFileName($Path);size=$bytes.Length;sha256=(Get-IconHash $Path);machine='AMD64';imports=@($imports);debugCRT=$false;testedPrivatePathsFound=$false}
}
function Test-IconProduct($Root, $RunRoot, $DependencyRoot) {
    $RunRoot = [IO.Path]::GetFullPath($RunRoot)
    $plan = Read-IconInputs $Root
    $identity = Read-S1StrictJson (Join-Path $RunRoot 'BUILD-IDENTITY.json')
    if ($identity.version -cne '0.1.2' -or $identity.scope -cne 'icon-v012' -or $identity.inputsSha256 -cne $plan.sha256) { throw 'ICON-012:BuildIdentity' }
    foreach ($binding in $identity.sourceBindings) { Assert-S1Identity (Resolve-S1Path $Root $binding.path) $binding.size $binding.sha256 }
    $null = Test-IconDependencies $DependencyRoot
    $original = Read-BR2SourceInputs $DependencyRoot
    $installed = Join-Path $DependencyRoot 'build/sec-br1-r1/installed/x64-windows-shipping-privacy/bin'
    $product = Join-Path $RunRoot product
    Assert-S1InputSet @(@('clipss2.exe') + $original.br1.inputs.productRuntimeDlls) @((Get-S1Files $product).Keys)
    foreach ($item in $identity.binaries) {
        Assert-S1Identity (Join-Path $product $item.path) $item.size $item.sha256
        if ($item.path -ne 'clipss2.exe' -and (Get-IconHash (Join-Path $installed $item.path)) -cne $item.sha256) { throw 'ICON-012:DependencyDLL' }
    }
    return [ordered]@{result='PASS';version='0.1.2';binaries=18;sourceCommit=$identity.sourceCommit;inputsSha256=$plan.sha256;releaseApproved=$false}
}
function Invoke-IconBuild($Root, $RunRoot, $DependencyRoot, $Jobs) {
    $commit = Get-IconGit $Root @('rev-parse','HEAD')
    $plan = Assert-IconBuildSource $Root $commit
    $null = Test-IconDependencies $DependencyRoot
    $null = Test-IconManaged $DependencyRoot
    $Root = [IO.Path]::GetFullPath($Root)
    $run = [IO.Path]::GetFullPath($RunRoot)
    $prefix = Join-Path $Root 'build/icon-phase3'
    if (!$run.StartsWith($prefix + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'ICON-012:RunRoot' }
    Assert-S1NoReparse $run
    if (Test-Path -LiteralPath $run) { throw 'ICON-012:RunExists' }
    Enter-IconDeveloperShell
    $null = New-Item -ItemType Directory -Path $run
    $null = New-Item -ItemType Directory -Path (Join-Path $run tmp)
    Write-IconJson (Join-Path $run owner.json) ([ordered]@{phase='ICON-3';sourceCommit=$commit;reuseAllowed=$false;releaseApproved=$false})
    $savedTemp=$env:TEMP; $savedTmp=$env:TMP
    $env:TEMP=Join-Path $run tmp; $env:TMP=$env:TEMP
    try {
        $dep = (Resolve-Path -LiteralPath $DependencyRoot).Path.Replace('\','/')
        $build = Join-Path $run build
        $cmakeArguments = @('-S',$Root,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_STANDARD=23',
            '-DCMAKE_CXX_STANDARD_REQUIRED=ON','-DCMAKE_CXX_EXTENSIONS=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
            '-DCLIPSS2_PUBLICATION_SCOPE=icon-v012','-DCLIPSS2_SHIPPING_PRIVACY=ON','-DCLIPSS2_SHIPPING_PROFILE=v2',
            "-DCLIPSS2_BUILD_SOURCE_COMMIT=$commit","-DCLIPSS2_ICON_RUN_ROOT=$($run.Replace('\','/'))",
            "-DCLIPSS2_ICON_DEPENDENCY_ROOT=$dep",'-DVCPKG_MANIFEST_INSTALL=OFF',
            "-DCMAKE_TOOLCHAIN_FILE=$dep/build/sec-br1-r1/vcpkg/scripts/buildsystems/vcpkg.cmake",
            "-DVCPKG_INSTALLED_DIR=$dep/build/sec-br1-r1/installed","-DVCPKG_MANIFEST_DIR=$dep/cmake/security/sec-br1",
            '-DVCPKG_TARGET_TRIPLET=x64-windows-shipping-privacy','-DVCPKG_HOST_TRIPLET=x64-windows',
            "-DVCPKG_OVERLAY_TRIPLETS=$dep/triplets","-DVCPKG_OVERLAY_PORTS=$dep/vcpkg-overlays/security-sec-br1;$dep/vcpkg-overlays/shipping-privacy-v2",
            "-DCLIPSS2_JPEGLI_SOURCE_DIR=$dep/build/managed-jpegli-src","-DCLIPSS2_DERIVED_JPEGLI_DIR=$dep/build/sec-br1-r1/managed-jpegli",
            '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF','-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF')
        & cmake @cmakeArguments 2>&1 | Tee-Object -FilePath (Join-Path $run configure.log) | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'ICON-012:Configure' }
        $cache=Get-Content -LiteralPath (Join-Path $build CMakeCache.txt) -Raw
        if($cache -notmatch 'CMAKE_RC_COMPILER:FILEPATH=[^\r\n]*[\\/]10\.0\.26100\.0[\\/]'){throw 'ICON-012:SDKVersion'}
        & cmake --build $build --parallel $Jobs 2>&1 | Tee-Object -FilePath (Join-Path $run build.log) | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'ICON-012:Build' }
        & ctest --test-dir $build --output-on-failure --output-junit (Join-Path $run ctest.xml) 2>&1 | Tee-Object -FilePath (Join-Path $run ctest.log) | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'ICON-012:CTest' }
        $null = Assert-IconBuildSource $Root $commit
        $null = Test-IconDependencies $DependencyRoot
        $null = Test-IconManaged $DependencyRoot
        $original = Read-BR2SourceInputs $DependencyRoot
        $product = Join-Path $run product
        $null = New-Item -ItemType Directory -Path $product
        foreach ($name in @(@('clipss2.exe') + $original.br1.inputs.productRuntimeDlls)) {
            $source = Join-Path $build $name
            if ($name -ne 'clipss2.exe' -and (Get-IconHash $source) -cne (Get-IconHash (Join-Path "$dep/build/sec-br1-r1/installed/x64-windows-shipping-privacy/bin" $name))) { throw 'ICON-012:BuiltDLLIdentity' }
            [IO.File]::Copy($source,(Join-Path $product $name),$false)
        }
        & (Join-Path $PSScriptRoot verify_app_icon.ps1) -Executable (Join-Path $product clipss2.exe) -ExpectedVersion '0.1.2' -ReportPath (Join-Path $run pe-icons.json) | Out-Null
        $commands = Get-Content -LiteralPath (Join-Path $build compile_commands.json) -Raw | ConvertFrom-Json -AsHashtable
        $projectCommands = @($commands | Where-Object { $_.file.Replace('\','/').StartsWith($Root.Replace('\','/')+'/src/',[StringComparison]::OrdinalIgnoreCase) })
        if (!$projectCommands.Count -or @($projectCommands | Where-Object { $_.command -notmatch '(?i)[/-]MD\b' -or $_.command -match '(?i)[/-](MDd|MTd|MT)\b' }).Count) { throw 'ICON-012:RuntimeFlags' }
        $binaries = @((Get-ChildItem -LiteralPath $product -File | Sort-Object Name) | ForEach-Object { Read-IconPE $_.FullName })
        $identity = [ordered]@{schemaVersion=1;version='0.1.2';scope='icon-v012';buildId='clipss2-0.1.2-icon-rc1';
            sourceCommit=$commit;sourceTree=(Get-IconGit $Root @('rev-parse',"$commit^{tree}"));inputsSha256=$plan.sha256;
            configuration='MSVC x64 Release /MD';msvcToolset='14.44.35207';compiler='19.44.35229.0';windowsSDK='10.0.26100.0';
            dependencyInputsSha256=$plan.inputs.dependencyInputsSha256;installedReceiptSha256=$plan.inputs.installedReceiptSha256;
            installedMarkerSha256=$plan.inputs.installedMarkerSha256;sourceBindings=@($plan.inputs.files);binaries=$binaries;
            projectCompileCommands=$projectCommands.Count;resourceGroupId=101;iconSizes=@(16,24,32,48,64,128,256);
            ctestFailures=0;runtimeBundled=$false;releaseApproved=$false;ownerAcceptance='PENDING'}
        Write-IconJson (Join-Path $run BUILD-IDENTITY.json) $identity
        Test-IconProduct $Root $run $DependencyRoot
    }
    finally { $env:TEMP=$savedTemp; $env:TMP=$savedTmp }
}
Export-ModuleMember -Function Get-IconHash,Write-IconJson,Get-IconBoundPaths,Read-IconInputs,Get-IconGit,Assert-IconBuildSource,Test-IconDependencies,Test-IconManaged,Read-IconPE,Test-IconProduct,Invoke-IconBuild
