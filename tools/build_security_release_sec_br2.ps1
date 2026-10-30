#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('PrepareContracts','PlanOnly','ValidateDependencies','VerifyManaged','Configure','ValidateProduct')][string]$Mode,
      [string]$Root=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ShippingProfileSecBR2.ps1)
if($Mode -ceq 'PrepareContracts'){Initialize-BR2ContractView $Root|ConvertTo-Json;exit 0}
$plan=Read-BR2SourceInputs $Root
switch($Mode){
    PlanOnly { [ordered]@{result='PASS';contractId=$plan.inputs.inputId;version='0.1.1';sourceInputsSha256=$plan.sha256;releaseApproved=$false}|ConvertTo-Json }
    ValidateDependencies { Test-BR2Dependencies $Root $plan|ConvertTo-Json }
    VerifyManaged {
        $paths=Get-BR1Paths $Root $plan.br1
        $null=Test-S1ManagedOriginal $Root $paths.original
        Test-S1ManagedDerivative $Root $paths.derived|ConvertTo-Json
    }
    Configure { Invoke-BR2Configure $Root $plan }
    ValidateProduct { Test-BR2Product $Root $plan|ConvertTo-Json }
}
