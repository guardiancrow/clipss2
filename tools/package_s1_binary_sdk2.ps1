#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('PlanOnly','CreatePrivateCandidate','ValidateCandidate','ValidateTree','ValidateZip')][string]$Mode,
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$R2EvidenceRoot,
    [Parameter(Mandatory)][string]$R3EvidenceRoot,
    [Parameter(Mandatory)][string]$R4BEvidenceRoot,
    [Parameter(Mandatory)][string]$R4DEvidenceRoot,
    [string]$AuthorizationPath,[string]$OutputRoot,[string]$CandidateRoot,[string]$PackageRoot,[string]$ZipPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinarySdk2Package.psm1)
try{switch($Mode){
    PlanOnly{
        if($OutputRoot -or $CandidateRoot -or $PackageRoot -or $ZipPath){throw 'S1:Sdk2:UnexpectedArgument'}
        Read-S1Sdk2Plan $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $AuthorizationPath | ConvertTo-Json
    }
    CreatePrivateCandidate{
        if(!$OutputRoot -or $CandidateRoot -or $PackageRoot -or $ZipPath){throw 'S1:Sdk2:UnexpectedArgument'}
        New-S1Sdk2Candidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $OutputRoot $AuthorizationPath | ConvertTo-Json -Depth 32
    }
    ValidateCandidate{
        if(!$CandidateRoot -or $OutputRoot -or $PackageRoot -or $ZipPath){throw 'S1:Sdk2:UnexpectedArgument'}
        Test-S1Sdk2Candidate $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $CandidateRoot $AuthorizationPath | ConvertTo-Json
    }
    ValidateTree{
        if(!$PackageRoot -or $OutputRoot -or $CandidateRoot -or $ZipPath){throw 'S1:Sdk2:UnexpectedArgument'}
        Test-S1Sdk2Tree $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $PackageRoot $AuthorizationPath | ConvertTo-Json
    }
    ValidateZip{
        if(!$ZipPath -or $OutputRoot -or $CandidateRoot -or $PackageRoot){throw 'S1:Sdk2:UnexpectedArgument'}
        Test-S1Sdk2Zip $Root $R2EvidenceRoot $R3EvidenceRoot $R4BEvidenceRoot $R4DEvidenceRoot $ZipPath $AuthorizationPath | ConvertTo-Json
    }
}}catch{Write-Error $_ -ErrorAction Continue;exit 1}
