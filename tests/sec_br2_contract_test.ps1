#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../tools/ShippingProfileSecBR2.ps1"
$source=(Resolve-Path "$PSScriptRoot/..").Path
$plan=Read-BR2SourceInputs $source
$legacy=Read-S1SourceInputs $source
$run=Join-Path $EvidenceRoot ('contracts-'+[guid]::NewGuid().ToString('N'))
New-S1OwnedDirectory $run
$utf8=[Text.UTF8Encoding]::new($false)
$results=[Collections.Generic.List[object]]::new()
function Case($Name,$Expected,$Body){
    $actual='ACCEPT'
    try{& $Body|Out-Null}catch{$actual=$_.Exception.Message}
    $ok=if($Expected){$actual.Contains($Expected,[StringComparison]::Ordinal)}else{$actual -ceq 'ACCEPT'}
    $results.Add([ordered]@{name=$Name;result=$(if($ok){'PASS'}else{'FAIL'});actual=$actual})
}
function New-View($Name){
    $view=Join-Path $run $Name
    New-S1OwnedDirectory $view
    $files=@($legacy.inputs.files.path)+@($plan.br1.inputs.files.path)+@($plan.inputs.files.path)+@(
        'cmake/privacy/source-inputs-s1-v1.json','cmake/security/sec-br2/build-inputs-v1.json',
        'tools/ShippingProfileSecBR2.ps1','tools/build_shipping_dependencies_sec_br1.ps1',
        'tools/build_shipping_dependencies_s1.ps1','tests/sec_br1_contract_test.ps1','tests/s1_dependency_install_test.ps1')
    foreach($relative in ($files|Select-Object -Unique)){
        $dest=Resolve-S1Path $view $relative
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
        [IO.File]::Copy((Join-Path $source $relative),$dest,$false)
    }
    return $view
}
Case 'new version with original dependency identity' '' {
    if($plan.inputs.version -cne '0.1.1' -or $plan.br1.sourceInputsSha256 -cne 'EF0EB35F031CE4424408709D0871C2A72F0FBF109A08C7295F911F40B4FFFE32'){throw 'Wrong identity'}
}
Case 'fresh historical contract reconstruction without Git' '' {
    $v=New-View prepare
    Initialize-BR2ContractView $v
    $p=Read-BR2SourceInputs $v
    if($p.br1.sourceInputsSha256 -cne $plan.br1.sourceInputsSha256){throw 'Changed historical identity'}
}
Case 'existing view refuses overwrite' 'BR2ContractViewExists' {
    $v=New-View occupied
    Initialize-BR2ContractView $v
    Initialize-BR2ContractView $v
}
Case 'versioned source mutation rejected' 'RawIdentity' {
    $v=New-View source-mutation
    Initialize-BR2ContractView $v
    [IO.File]::AppendAllText((Join-Path $v src/main.cpp),' ',$utf8)
    Read-BR2SourceInputs $v
}
Case 'historical preimage mutation rejected' 'RawIdentity' {
    $v=New-View old-mutation
    Initialize-BR2ContractView $v
    [IO.File]::AppendAllText((Join-Path $v 'build/sec-br2/br1-source/CMakeLists.txt'),' ',$utf8)
    Read-BR2SourceInputs $v
}
Case 'current Brotli port remains pinned' 'RawIdentity' {
    $v=New-View port-mutation
    Initialize-BR2ContractView $v
    [IO.File]::AppendAllText((Join-Path $v 'vcpkg-overlays/security-sec-br1/brotli/portfile.cmake'),' ',$utf8)
    Read-BR2SourceInputs $v
}
Case 'current overlay closure rejects unknown port' 'InputSet' {
    $v=New-View extra-port
    Initialize-BR2ContractView $v
    $file=Join-Path $v 'vcpkg-overlays/security-sec-br1/foreign/vcpkg.json'
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))
    [IO.File]::WriteAllText($file,'{}',$utf8)
    Read-BR2SourceInputs $v
}
Case 'new ledger hash cannot be silently changed' 'BR2Ledger' {
    $v=New-View ledger-mutation
    [IO.File]::AppendAllText((Join-Path $v 'cmake/security/sec-br2/build-inputs-v1.json'),' ',$utf8)
    Read-BR2SourceInputs $v
}
$summary=[ordered]@{result=$(if(@($results|Where-Object result -CEQ FAIL).Count){'FAIL'}else{'PASS'});kind='offline contract fixtures';cases=$results;actualBuild=$false;releaseApproved=$false}
Write-S1NewJson (Join-Path $run results.json) $summary
$summary|ConvertTo-Json -Depth 6
if($summary.result -ceq 'FAIL'){exit 1}
