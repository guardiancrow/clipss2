#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
param([Parameter(Mandatory)][ValidateSet('CreateProfile','PlanOnly','CreateCandidate','ValidateCandidate')][string]$Mode,
    [Parameter(Mandatory)][string]$DependencyRoot,[string]$RunRoot,[string]$Root=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/IconV012Package.psm1) -Force -DisableNameChecking
switch($Mode) {
    CreateProfile { $p=New-IconPackageProfile $Root $RunRoot $DependencyRoot; [ordered]@{result='PASS';version=$p.version;files=$p.files.Count}|ConvertTo-Json }
    PlanOnly { $p=Read-IconPackageProfile $Root $DependencyRoot; [ordered]@{result='PASS';version=$p.version;files=$p.files.Count;releaseApproved=$false}|ConvertTo-Json }
    CreateCandidate { New-IconCandidate $Root $DependencyRoot|ConvertTo-Json -Depth 6 }
    ValidateCandidate { Test-IconCandidate $Root $DependencyRoot|ConvertTo-Json -Depth 6 }
}
