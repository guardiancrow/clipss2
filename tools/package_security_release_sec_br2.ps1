#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
param([Parameter(Mandatory)][ValidateSet('PlanOnly','CreateCandidate','ValidateCandidate')][string]$Mode,
      [string]$Root=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/SecBR2Package.psm1)
switch($Mode){
    PlanOnly {$p=Read-BR2PackageProfile $Root;[ordered]@{result='PASS';version=$p.version;files=$p.files.Count;productSourceCommit=$p.productSourceCommit;binaryReleaseApproved=$false}|ConvertTo-Json}
    CreateCandidate {New-BR2Candidate $Root|ConvertTo-Json -Depth 6}
    ValidateCandidate {Test-BR2Candidate $Root|ConvertTo-Json -Depth 6}
}
