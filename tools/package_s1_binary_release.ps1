#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('PlanOnly','CreatePrivateCandidate','ValidateTree','ValidateZip','ValidateCandidate')][string]$Mode,
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$R2EvidenceRoot,
    [Parameter(Mandatory)][string]$R3EvidenceRoot,
    [string]$AuthorizationPath,
    [string]$OutputRoot,
    [string]$PackageRoot,
    [string]$ZipPath,
    [string]$CandidateRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinaryReleasePackage.psm1)
try{
    switch($Mode){
        PlanOnly{
            if($OutputRoot -or $PackageRoot -or $ZipPath -or $CandidateRoot){throw 'S1:R4B:UnexpectedArgument'}
            Read-S1ReleasePackagingPlan $Root $R2EvidenceRoot $R3EvidenceRoot $AuthorizationPath | ConvertTo-Json
        }
        CreatePrivateCandidate{
            if(!$OutputRoot -or $PackageRoot -or $ZipPath -or $CandidateRoot){throw 'S1:R4B:UnexpectedArgument'}
            New-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $OutputRoot $AuthorizationPath | ConvertTo-Json -Depth 32
        }
        ValidateTree{
            if(!$PackageRoot -or $OutputRoot -or $ZipPath -or $CandidateRoot){throw 'S1:R4B:UnexpectedArgument'}
            Test-S1ReleaseCandidateTree $Root $R2EvidenceRoot $R3EvidenceRoot $PackageRoot $AuthorizationPath | ConvertTo-Json
        }
        ValidateZip{
            if(!$ZipPath -or $OutputRoot -or $PackageRoot -or $CandidateRoot){throw 'S1:R4B:UnexpectedArgument'}
            Test-S1ReleaseCandidateZip $Root $R2EvidenceRoot $R3EvidenceRoot $ZipPath $AuthorizationPath | ConvertTo-Json
        }
        ValidateCandidate{
            if(!$CandidateRoot -or $OutputRoot -or $PackageRoot -or $ZipPath){throw 'S1:R4B:UnexpectedArgument'}
            Test-S1ReleasePrivateCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $CandidateRoot $AuthorizationPath | ConvertTo-Json
        }
    }
}catch{Write-Error $_ -ErrorAction Continue;exit 1}
