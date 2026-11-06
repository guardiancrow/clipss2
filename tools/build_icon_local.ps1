#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DependencyRoot,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$OutputRoot,
    [ValidateRange(1, 32)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$dependencyRootPath = (Resolve-Path -LiteralPath $DependencyRoot).Path.Replace('\', '/')
$allowedRoot = Join-Path $sourceRoot 'build/icon-phase2'
if (!$OutputRoot) { $OutputRoot = Join-Path $allowedRoot ($Configuration.ToLowerInvariant() + '-' + [guid]::NewGuid().ToString('N')) }
$outputPath = [IO.Path]::GetFullPath($OutputRoot)
if (!$outputPath.StartsWith($allowedRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must be a fresh directory under build/icon-phase2/.'
}
if (Test-Path -LiteralPath $outputPath) { throw 'OutputRoot already exists; preserve it and choose a fresh path.' }
if (!(Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'MSVC x64 developer tools are required.' }
    Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}
$null = New-Item -ItemType Directory -Path $outputPath
$productPath = Join-Path $outputPath 'product'
$tempPath = Join-Path $outputPath 'tmp'
$null = New-Item -ItemType Directory -Path $tempPath
$savedTemp = $env:TEMP
$savedTmp = $env:TMP
$env:TEMP = $tempPath
$env:TMP = $tempPath
try {
    $arguments = @('-S', $sourceRoot, '-B', $productPath, '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$Configuration", '-DCMAKE_CXX_STANDARD=23',
        '-DCMAKE_CXX_STANDARD_REQUIRED=ON', '-DCMAKE_CXX_EXTENSIONS=OFF',
        '-DCLIPSS2_PUBLICATION_SCOPE=icon-local', '-DCLIPSS2_SHIPPING_PRIVACY=ON', '-DCLIPSS2_SHIPPING_PROFILE=v2',
        "-DCLIPSS2_ICON_DEPENDENCY_ROOT=$dependencyRootPath", '-DVCPKG_MANIFEST_INSTALL=OFF',
        "-DCMAKE_TOOLCHAIN_FILE=$dependencyRootPath/build/sec-br1-r1/vcpkg/scripts/buildsystems/vcpkg.cmake",
        "-DVCPKG_INSTALLED_DIR=$dependencyRootPath/build/sec-br1-r1/installed",
        "-DVCPKG_MANIFEST_DIR=$dependencyRootPath/cmake/security/sec-br1",
        '-DVCPKG_TARGET_TRIPLET=x64-windows-shipping-privacy', '-DVCPKG_HOST_TRIPLET=x64-windows',
        "-DVCPKG_OVERLAY_TRIPLETS=$dependencyRootPath/triplets",
        "-DVCPKG_OVERLAY_PORTS=$dependencyRootPath/vcpkg-overlays/security-sec-br1;$dependencyRootPath/vcpkg-overlays/shipping-privacy-v2",
        "-DCLIPSS2_JPEGLI_SOURCE_DIR=$dependencyRootPath/build/managed-jpegli-src",
        "-DCLIPSS2_DERIVED_JPEGLI_DIR=$dependencyRootPath/build/sec-br1-r1/managed-jpegli",
        '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF')
    & cmake @arguments 2>&1 | Tee-Object -FilePath (Join-Path $outputPath 'configure.log')
    if ($LASTEXITCODE -ne 0) { throw 'ICON-LOCAL configure failed; output preserved.' }
    & cmake --build $productPath --parallel $Jobs 2>&1 | Tee-Object -FilePath (Join-Path $outputPath 'build.log')
    if ($LASTEXITCODE -ne 0) { throw 'ICON-LOCAL build failed; output preserved.' }
    & ctest --test-dir $productPath --output-on-failure --output-junit (Join-Path $outputPath 'ctest.xml') 2>&1 |
        Tee-Object -FilePath (Join-Path $outputPath 'ctest.log')
    if ($LASTEXITCODE -ne 0) { throw 'ICON-LOCAL CTest failed; output preserved.' }
    Write-Output "ICON-LOCAL validation complete: $outputPath (developer use only; no release approval)"
}
finally {
    $env:TEMP = $savedTemp
    $env:TMP = $savedTmp
}
