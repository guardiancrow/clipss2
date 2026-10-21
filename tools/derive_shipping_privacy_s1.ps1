#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param($Mode,$Profile,$Scope,$Privacy,$Configuration,$Architecture,
      $Root=(Split-Path $PSScriptRoot -Parent),$Original='',$Destination='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ShippingProfileS1.ps1)
Assert-S1String $Mode
if($Mode -cnotin @('VerifyOnly','Derive')){Stop-S1 Mode}
Assert-S1Selection $Profile $Scope $Privacy $Configuration $Architecture
$plan=Read-S1SourceInputs $Root
if($Original -isnot [string]){Stop-S1 ScalarString}
$originalRoot=if($Original -ceq ''){Resolve-S1Path $Root 'build/managed-jpegli-src'}else{
    if(![IO.Path]::IsPathFullyQualified($Original)){Stop-S1 OriginalRoot}
    Assert-S1NoReparse $Original
    [IO.Path]::GetFullPath($Original).TrimEnd([char[]]@('/','\'))
}
$derivedRoot=Get-S1ExpectedRoot $Root 'build/managed-jpegli-s1-v2' $Destination
if($originalRoot -ceq $derivedRoot -or $derivedRoot.StartsWith($originalRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or $originalRoot.StartsWith($derivedRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){Stop-S1 OriginalRoot}
if($Mode -ceq 'Derive' -and ([IO.Directory]::Exists($derivedRoot) -or [IO.File]::Exists($derivedRoot))){Stop-S1 OutputExists}
$managed=Test-S1ManagedOriginal $Root $originalRoot
if($Mode -ceq 'VerifyOnly'){
    $derivedValidated=$false
    if([IO.Directory]::Exists($derivedRoot)){$null=Test-S1ManagedDerivative $Root $derivedRoot;$derivedValidated=$true}
    # CMake passes an explicit Destination and requires actual derived bytes.
    if($PSBoundParameters.ContainsKey('Destination') -and !$derivedValidated){Stop-S1 DerivedMissing}
    [ordered]@{result='PASS';mode='VerifyOnly';originalFiles=1481;originalCheckouts=4;gitlinks=9;derivedValidated=$derivedValidated;writes=$false;build=$false}|ConvertTo-Json
    exit 0
}
# No outputs exist until all required contracts and original source are accepted.
[void][IO.Directory]::CreateDirectory($derivedRoot)
foreach($row in $managed.rows){
    $bytes=[IO.File]::ReadAllBytes((Resolve-S1Path $originalRoot $row.path))
    foreach($change in $managed.spec.changes){if($row.path -ceq $change.path){
        if((Get-S1Hash $bytes) -cne $change.originalSha256){Stop-S1 StagePreimage}
        $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
        if([regex]::Matches($text,[regex]::Escape($change.old)).Count -ne $change.occurrences){Stop-S1 StageOccurrences}
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes($text.Replace($change.old,$change.replacement))
        if((Get-S1Hash $bytes) -cne $change.derivedSha256){Stop-S1 StagePostimage}
    }}
    if((Get-S1Hash $bytes) -cne $row.derivedSha256){Stop-S1 DerivedPostimage}
    $out=Resolve-S1Path $derivedRoot $row.path
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($out))
    $stream=[IO.File]::Open($out,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
$verified=Test-S1ManagedDerivative $Root $derivedRoot
[ordered]@{result=$verified.result;mode='Derive';files=1481;stages=3;sourceTransformationOnly=$true;gitModesBoundToOriginalAndManifest=$true;posixModeMeasured=$false;compile=$false;install=$false;abi=$false;runtime=$false}|ConvertTo-Json
