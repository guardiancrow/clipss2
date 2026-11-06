#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [string]$Ico = (Join-Path $PSScriptRoot '../src/platform/windows/assets/clipss2.ico'),
    [string]$ExpectedVersion = '0.1.1',
    [string]$ReportPath
)
$ErrorActionPreference = 'Stop'
if ($ReportPath -and (Test-Path -LiteralPath $ReportPath)) { throw 'Report already exists; preserve it and choose another path.' }
if (!('Clipss2IconResources' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class Clipss2IconResources {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr LockResource(IntPtr resource);
    public static byte[] Read(IntPtr module, int type, int id) {
        var resource = FindResourceW(module, (IntPtr)id, (IntPtr)type);
        if (resource == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        var size = SizeofResource(module, resource);
        var data = LockResource(LoadResource(module, resource));
        if (size == 0 || data == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        var bytes = new byte[checked((int)size)];
        Marshal.Copy(data, bytes, 0, bytes.Length);
        return bytes;
    }
}
'@
}
$exePath = (Resolve-Path -LiteralPath $Executable).Path
$icoBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Ico).Path)
$module = [Clipss2IconResources]::LoadLibraryExW($exePath, [IntPtr]::Zero, 0x22)
if ($module -eq [IntPtr]::Zero) { throw 'Cannot map EXE resources (no code execution requested).' }
function U16($Bytes, $Offset) { [BitConverter]::ToUInt16($Bytes, $Offset) }
function U32($Bytes, $Offset) { [BitConverter]::ToUInt32($Bytes, $Offset) }
function Hash($Bytes) { [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([byte[]]$Bytes)) }
try {
    $group = [Clipss2IconResources]::Read($module, 14, 101)
    if ((U16 $group 0) -ne 0 -or (U16 $group 2) -ne 1 -or (U16 $group 4) -ne 7) { throw 'Unexpected RT_GROUP_ICON header.' }
    if ((U16 $icoBytes 0) -ne 0 -or (U16 $icoBytes 2) -ne 1 -or (U16 $icoBytes 4) -ne 7) { throw 'Unexpected ICO header.' }
    $expectedSizes = @(16,24,32,48,64,128,256)
    $entries = for ($index = 0; $index -lt 7; ++$index) {
        $icoOffset = 6 + $index * 16
        $groupOffset = 6 + $index * 14
        $size = if ($group[$groupOffset] -eq 0) { 256 } else { [int]$group[$groupOffset] }
        if ($size -ne $expectedSizes[$index] -or $group[$groupOffset] -ne $group[$groupOffset + 1]) { throw 'Unexpected embedded icon dimensions.' }
        if ((U16 $group ($groupOffset + 6)) -ne 32) { throw 'Icon must be 32-bit.' }
        $id = U16 $group ($groupOffset + 12)
        $embedded = [Clipss2IconResources]::Read($module, 3, $id)
        $length = U32 $icoBytes ($icoOffset + 8)
        $offset = U32 $icoBytes ($icoOffset + 12)
        $original = [byte[]]::new($length)
        [Array]::Copy($icoBytes, $offset, $original, 0, $length)
        if ($embedded.Length -ne $length -or (Hash $embedded) -cne (Hash $original)) { throw "RT_ICON $id differs from adopted ICO." }
        if ((U32 $group ($groupOffset + 8)) -ne $length) { throw 'RT_GROUP_ICON length mismatch.' }
        if ([Convert]::ToHexString($embedded[0..7]) -ne '89504E470D0A1A0A' -or $embedded[25] -ne 6) { throw 'Expected embedded RGBA PNG.' }
        [ordered]@{ size=$size; resourceId=$id; format='RGBA PNG'; exactAdoptedBytes=$true; sha256=(Hash $embedded) }
    }
    $versionResource = [Clipss2IconResources]::Read($module, 16, 1)
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($exePath)
    if ($version.FileVersion -cne "$ExpectedVersion.0" -or $version.ProductVersion -cne $ExpectedVersion) { throw 'VERSIONINFO changed unexpectedly.' }
    $manifest = [Text.Encoding]::UTF8.GetString([Clipss2IconResources]::Read($module, 24, 1)).Trim([char]0, [char]0xFEFF)
    $manifestXml = [xml]$manifest
    foreach ($item in @(@('dpiAware','true/pm'), @('dpiAwareness','PerMonitorV2, PerMonitor'), @('consoleAllocationPolicy','detached'))) {
        $element = $manifestXml.SelectSingleNode("//*[local-name()='$($item[0])']")
        if (!$element -or $element.InnerText -cne $item[1]) { throw "Manifest setting not retained: $($item[0])" }
    }
    $report = [ordered]@{
        result='PASS'; executable=[IO.Path]::GetFileName($exePath); exeSha256=(Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
        iconGroupId=101; iconFrames=@($entries); icoSha256=(Hash $icoBytes)
        versionResourcePresent=($versionResource.Length -gt 0); fileVersion=$version.FileVersion; productVersion=$version.ProductVersion
        manifestPresent=$true; perMonitorV2Preserved=$true; detachedConsolePolicyPreserved=$true
        scope='Local validation only; not a published source/binary identity or release acceptance.'
    }
    if ($ReportPath) { $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding utf8 }
    $report | ConvertTo-Json -Depth 6
}
finally { [void][Clipss2IconResources]::FreeLibrary($module) }
