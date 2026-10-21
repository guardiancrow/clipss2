#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param($Mode,$Profile,$Scope,$Privacy,$Configuration,$Architecture,
      $Root=(Split-Path $PSScriptRoot -Parent),$InstalledDir='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ShippingProfileS1.ps1)
Assert-S1String $Mode
if($Mode -cnotin @('PlanOnly','ValidateInstalled','Install')){Stop-S1 Mode}
Assert-S1Selection $Profile $Scope $Privacy $Configuration $Architecture
if($InstalledDir -isnot [string]){Stop-S1 ScalarString}
if($Mode -ceq 'Install'){Stop-S1 InstallNotImplemented}
if($Mode -ceq 'PlanOnly'){
    if($null -ne $InstalledDir -and $InstalledDir -cne ''){Stop-S1 Mode}
    Get-S1Plan $Root $Profile $Scope $Privacy $Configuration $Architecture|ConvertTo-Json -Depth 10
    exit 0
}
Assert-S1String $InstalledDir
$plan=Read-S1SourceInputs $Root
Test-S1Installed $Root $InstalledDir $plan|ConvertTo-Json -Depth 5
