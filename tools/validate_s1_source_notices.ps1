#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
param([switch]$ValidateSourceState,[switch]$ValidateSourceRelease,[switch]$ValidateCandidate,[switch]$PublicationView,$Root=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
if($ValidateCandidate){throw 'S1:CandidateContractUnsupported'}
if(([int][bool]$ValidateSourceState+[int][bool]$ValidateSourceRelease) -ne 1){throw 'S1:SourceValidationMode'}
Import-Module (Join-Path $PSScriptRoot ../packaging/S1SourceNotice.psm1) -Force
if($ValidateSourceRelease){Read-S1SourceRelease $Root -PublicationView:$PublicationView|ConvertTo-Json -Depth 6}
else{Read-S1SourceState $Root -PublicationView:$PublicationView|ConvertTo-Json -Depth 6}
