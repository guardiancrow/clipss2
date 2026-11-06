#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('PlanOnly','ValidateSource','ValidateDependencies','VerifyManaged','Build','ValidateProduct')][string]$Mode,
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [string]$DependencyRoot,
    [string]$RunRoot,
    [string]$ExpectedCommit,
    [ValidateRange(1,32)][int]$Jobs=4
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot IconV012.psm1) -Force -DisableNameChecking
$plan=Read-IconInputs $Root
switch($Mode) {
    PlanOnly { [ordered]@{result='PASS';scope='icon-v012';version='0.1.2';files=$plan.inputs.files.Count;inputsSha256=$plan.sha256;releaseApproved=$false}|ConvertTo-Json }
    ValidateSource { $null=Assert-IconBuildSource $Root $ExpectedCommit; [ordered]@{result='PASS';sourceCommit=$ExpectedCommit;inputsSha256=$plan.sha256}|ConvertTo-Json }
    ValidateDependencies { Test-IconDependencies $DependencyRoot|ConvertTo-Json -Depth 6 }
    VerifyManaged { Test-IconManaged $DependencyRoot|ConvertTo-Json -Depth 6 }
    Build { Invoke-IconBuild $Root $RunRoot $DependencyRoot $Jobs|ConvertTo-Json -Depth 6 }
    ValidateProduct { Test-IconProduct $Root $RunRoot $DependencyRoot|ConvertTo-Json -Depth 6 }
}
