#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('PlanOnly','CreatePrivateCandidate','ValidateCandidate','ValidateTree','ValidateZip')][string]$Mode,
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$R2EvidenceRoot,
    [Parameter(Mandatory)][string]$R3EvidenceRoot,
    [Parameter(Mandatory)][string]$R4BEvidenceRoot,
    [string]$AuthorizationPath,[string]$OutputRoot,[string]$CandidateRoot,[string]$PackageRoot,[string]$ZipPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinaryFinalPackage.psm1)
try{switch($Mode){
    PlanOnly{
        if($OutputRoot -or $CandidateRoot -or $PackageRoot -or $ZipPath){throw 'S1:Final:UnexpectedArgument'}
        Read-S1FinalPlan $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $AuthorizationPath | ConvertTo-Json
    }
    CreatePrivateCandidate{
        if(!$OutputRoot -or $CandidateRoot -or $PackageRoot -or $ZipPath){throw 'S1:Final:UnexpectedArgument'}
        New-S1FinalCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $OutputRoot $AuthorizationPath | ConvertTo-Json -Depth 32
    }
    ValidateCandidate{
        if(!$CandidateRoot -or $OutputRoot -or $PackageRoot -or $ZipPath){throw 'S1:Final:UnexpectedArgument'}
        Test-S1FinalCandidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $CandidateRoot $AuthorizationPath | ConvertTo-Json
    }
    ValidateTree{
        if(!$PackageRoot -or $OutputRoot -or $CandidateRoot -or $ZipPath){throw 'S1:Final:UnexpectedArgument'}
        Test-S1FinalTree $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $PackageRoot $AuthorizationPath | ConvertTo-Json
    }
    ValidateZip{
        if(!$ZipPath -or $OutputRoot -or $CandidateRoot -or $PackageRoot){throw 'S1:Final:UnexpectedArgument'}
        Test-S1FinalZip $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $ZipPath $AuthorizationPath | ConvertTo-Json
    }
}}catch{Write-Error $_ -ErrorAction Continue;exit 1}
