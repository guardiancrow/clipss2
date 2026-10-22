#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('PlanOnly','CreatePrivateCandidate','ValidateTree','ValidateZip')][string]$Mode,
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$EvidenceRoot,
    [string]$OutputRoot,
    [string]$PackageRoot,
    [string]$ZipPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinaryPackage.psm1)
try {
    switch ($Mode) {
        PlanOnly {
            if ($OutputRoot -or $PackageRoot -or $ZipPath) { throw 'S1:Binary:UnexpectedArgument' }
            $result = Read-S1BinaryPlan $Root $EvidenceRoot
            $result | Select-Object result,mode,PE,files,licenses,binaryReleaseApproved,publicDistributionAllowed,openRights | ConvertTo-Json
        }
        CreatePrivateCandidate {
            if (!$OutputRoot -or $PackageRoot -or $ZipPath) { throw 'S1:Binary:UnexpectedArgument' }
            New-S1BinaryCandidate $Root $EvidenceRoot $OutputRoot | ConvertTo-Json -Depth 32
        }
        ValidateTree {
            if (!$PackageRoot -or $OutputRoot -or $ZipPath) { throw 'S1:Binary:UnexpectedArgument' }
            Test-S1BinaryTree $Root $EvidenceRoot $PackageRoot | ConvertTo-Json
        }
        ValidateZip {
            if (!$ZipPath -or $OutputRoot -or $PackageRoot) { throw 'S1:Binary:UnexpectedArgument' }
            Test-S1BinaryZip $Root $EvidenceRoot $ZipPath | ConvertTo-Json
        }
    }
} catch { Write-Error $_ -ErrorAction Continue; exit 1 }
