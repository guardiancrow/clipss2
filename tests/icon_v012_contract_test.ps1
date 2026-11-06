#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $root tools/IconV012.psm1) -Force -DisableNameChecking
$plan=Read-IconInputs $root
$run=Join-Path ([IO.Path]::GetFullPath($EvidenceRoot)) ('contracts-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $run
$results=[Collections.Generic.List[object]]::new()
function New-View($Name) {
    $view=Join-Path $run $Name
    $null=New-Item -ItemType Directory -Path $view
    foreach($relative in @($plan.inputs.files.path)+@('cmake/icon-v012/source-inputs-v1.json')) {
        $dest=Join-Path $view $relative
        $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        [IO.File]::Copy((Join-Path $root $relative),$dest,$false)
    }
    return $view
}
function Case($Name,$Expected,$Action) {
    $actual='ACCEPT'
    try { & $Action|Out-Null } catch { $actual=$_.Exception.Message }
    $ok=if($Expected){$actual.Contains($Expected,[StringComparison]::Ordinal)}else{$actual -ceq 'ACCEPT'}
    $results.Add([ordered]@{name=$Name;result=$(if($ok){'PASS'}else{'FAIL'});actual=$actual})
}
Case 'complete original candidate source inputs' '' { Read-IconInputs (New-View clean) }
foreach($pair in @(@('icon','src/platform/windows/assets/clipss2.ico'),
    @('resource-id','src/platform/windows/app_resources.h'),
    @('resource-template','src/platform/windows/app_icon.rc.in'),
    @('manifest','src/platform/windows/clipss2.exe.manifest'),
    @('compiler-policy','CMakeLists.txt'))) {
    $name=$pair[0];$relative=$pair[1]
    Case "changed $name rejected" 'RawIdentity' {
        $view=New-View $name
        $file=Join-Path $view $relative
        $bytes=[IO.File]::ReadAllBytes($file);$bytes[0]=$bytes[0] -bxor 1
        [IO.File]::WriteAllBytes($file,$bytes)
        Read-IconInputs $view
    }
}
Case 'unbound compile unit rejected' 'InputSet' {
    $view=New-View extra-source
    [IO.File]::WriteAllText((Join-Path $view src/unbound.cpp),'int unbound;')
    Read-IconInputs $view
}
Case 'changed dependency contract rejected' 'InputContract' {
    $view=New-View dependency
    $file=Join-Path $view cmake/icon-v012/source-inputs-v1.json
    $value=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json -AsHashtable
    $value.dependencyInputsSha256='0'*64
    [IO.File]::WriteAllText($file,($value|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
    Read-IconInputs $view
}
Case 'wrong immutable build commit rejected' 'BuildCommit' { Assert-IconBuildSource $root ('0'*40) }
Write-IconJson (Join-Path $run results.json) @($results)
$results|ConvertTo-Json -Depth 5
if(@($results|Where-Object result -CEQ FAIL).Count){throw 'ICON-012 contract tests failed'}
