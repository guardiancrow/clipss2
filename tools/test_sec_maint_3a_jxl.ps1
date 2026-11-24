#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [ValidateRange(10,100)][int]$Repetitions=10
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot ShippingProfileSecBR1.ps1)
$root=Split-Path $PSScriptRoot -Parent
$run=[IO.Path]::GetFullPath($RunRoot)
$prefix=(Join-Path $root 'build/sec-maint-v013/3a')+[IO.Path]::DirectorySeparatorChar
if(!$run.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){Stop-S1 MaintRunRoot}
Assert-S1NoReparse $run
$evidence=Join-Path $run ('jxl-evidence-'+[guid]::NewGuid().ToString('N'))
New-S1OwnedDirectory $evidence
$probes=[ordered]@{
    reference=Join-Path $run 'build/jxl-probes/reference/clipss2_maint_jxl_reference.exe'
    candidate=Join-Path $run 'build/jxl-probes/candidate/clipss2_maint_jxl_candidate.exe'
}
$expected=[ordered]@{reference='12000';candidate='13000'}
function Invoke-MaintProbe($Variant,[string[]]$Arguments){
    $result=& $probes[$Variant] @Arguments
    if($LASTEXITCODE -ne 0){Stop-S1 ('MaintJxlProbe:'+ $Variant+'/'+($Arguments[0..1] -join '/'))}
    return $result
}
$cross=@(foreach($fixture in 'odd','large','texture'){
    foreach($encoder in 'reference','candidate'){
        $decoder=if($encoder -ceq 'reference'){'candidate'}else{'reference'}
        $file=Join-Path $evidence "$fixture-$encoder.jxl"
        Invoke-MaintProbe $encoder @('encode',$fixture,$file)
        Invoke-MaintProbe $decoder @('decode',$fixture,$file)
        [ordered]@{fixture=$fixture;encoder=$expected[$encoder];decoder=$expected[$decoder];
            sha256=(Get-FileHash -LiteralPath $file).Hash;result='PASS'}
    }
})
Write-S1NewJson (Join-Path $evidence 'cross-decode.json') $cross

$culture=[Globalization.CultureInfo]::InvariantCulture
$trials=@(foreach($fixture in 'odd','large','texture'){
    foreach($variant in $probes.Keys){$null=Invoke-MaintProbe $variant @('bench',$fixture)}
    for($i=1;$i -le $Repetitions;$i++){
        foreach($variant in $probes.Keys){
            $fields=(Invoke-MaintProbe $variant @('bench',$fixture)) -split ','
            if($fields.Count -ne 6 -or $fields[0] -cne $expected[$variant]){Stop-S1 MaintJxlMeasurement}
            [ordered]@{fixture=$fixture;variant=$variant;version=[int]$fields[0];threads=[int]$fields[1];trial=$i;
                encodeMs=[double]::Parse($fields[2],$culture);decodeMs=[double]::Parse($fields[3],$culture);
                sizeBytes=[long]$fields[4];peakWorkingSetBytes=[long]$fields[5]}
        }
    }
})
Write-S1NewJson (Join-Path $evidence 'trials.json') $trials
$threads=@($trials.threads | Sort-Object -Unique)
if($threads.Count -ne 1){Stop-S1 MaintJxlThreadCount}
$summary=@(foreach($group in ($trials | Group-Object -Property { $_.fixture }, { $_.variant })){
    $metrics=[ordered]@{}
    foreach($metric in 'encodeMs','decodeMs','sizeBytes','peakWorkingSetBytes'){
        $sorted=@($group.Group | ForEach-Object { $_[$metric] } | Sort-Object)
        $middle=[int][Math]::Floor($sorted.Count/2)
        $median=if($sorted.Count % 2){$sorted[$middle]}else{($sorted[$middle-1]+$sorted[$middle])/2}
        $metrics[$metric]=[ordered]@{median=$median;min=$sorted[0];max=$sorted[-1]}
    }
    [ordered]@{fixture=$group.Group[0].fixture;variant=$group.Group[0].variant;metrics=$metrics}
})
Write-S1NewJson (Join-Path $evidence 'summary.json') ([ordered]@{threads=$threads[0];
    effort=7;trialsPerVariant=$Repetitions;results=$summary})
[ordered]@{result='PASS';crossDecode=$cross.Count;threads=$threads[0];trials=$trials.Count;
    evidenceDirectory=(Split-Path -Leaf $evidence);results=$summary}|ConvertTo-Json -Depth 7
