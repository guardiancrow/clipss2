#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('PlanOnly','Derive','VerifyManaged','Install','ValidateInstalled','Configure','ValidateProduct')][string]$Mode,
      [string]$Root=(Split-Path $PSScriptRoot -Parent),[switch]$BuildToolsEligibilityConfirmed)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ShippingProfileSecBR1.ps1)
$plan=Read-BR1SourceInputs $Root
switch($Mode){
    PlanOnly {
        [ordered]@{result='PASS';contractId=$plan.inputs.inputId;sourceInputsSha256=$plan.sourceInputsSha256;
            packages=$plan.inputs.packages;outputPolicy=$plan.inputs.outputPolicy;
            actualInstallPerformed=$false;abiVerified=$false;binaryReleaseApproved=$false}|ConvertTo-Json -Depth 10
    }
    Derive { Invoke-BR1Derive $Root $plan | ConvertTo-Json }
    VerifyManaged {
        $paths=Get-BR1Paths $Root $plan
        $null=Test-S1ManagedOriginal $Root $paths.original
        Test-S1ManagedDerivative $Root $paths.derived | ConvertTo-Json
    }
    Install { Invoke-BR1Install $Root -BuildToolsEligibilityConfirmed:$BuildToolsEligibilityConfirmed | ConvertTo-Json }
    ValidateInstalled { Test-BR1Installed $Root $plan | ConvertTo-Json }
    ValidateProduct { Test-BR1Product $Root $plan | ConvertTo-Json }
    Configure { Invoke-BR1Configure $Root $plan }
}
