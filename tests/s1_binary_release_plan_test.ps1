#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$R2EvidenceRoot,
    [Parameter(Mandatory)][string]$R3EvidenceRoot,
    [Parameter(Mandatory)][string]$TestRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinaryReleasePlan.psm1)
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$Root=[IO.Path]::GetFullPath($Root)
$TestRoot=[IO.Path]::GetFullPath($TestRoot)
Assert-S1NoReparse $TestRoot
if (!$TestRoot.StartsWith((Join-Path $Root build)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $TestRoot)) { throw 'New owned fixture under workspace/build required' }
[void][IO.Directory]::CreateDirectory($TestRoot)
$results=[Collections.Generic.List[object]]::new()
function Check($Name,[scriptblock]$Action) {
    & $Action
    $results.Add([ordered]@{name=$Name;result='PASS'})
    Write-Host "PASS $Name"
}
function Refuse($Name,[scriptblock]$Action) {
    $message=$null
    try{$null=& $Action}catch{$message=$_.Exception.Message}
    if($message -cne 'S1:RawIdentity'){throw "Expected identity refusal: $Name; actual=$message"}
    $results.Add([ordered]@{name=$Name;result='PASS';refusal=$message})
    Write-Host "PASS $Name"
}
$profile=Read-S1BinaryReleaseProfile $Root
$r3Profile=Read-S1StrictJson (Join-Path $Root $profile.parentProfile.path)
$protected=@((Join-Path $Root $profile.parentProfile.path),(Join-Path $Root packaging/S1BinaryPackage.psm1),
    (Join-Path $Root tools/package_s1_binary.ps1),(Join-Path $Root docs/BINARY-S1-PACKAGING.md))
$protected+=@($r3Profile.files|ForEach-Object {Join-Path $Root $_.source})
$protected+=@($profile.files|ForEach-Object {Join-Path $Root $_.source})
$protected+=@($profile.r2Bindings|ForEach-Object {Join-Path $R2EvidenceRoot $_.path})
$protected+=@($profile.r3Bindings|ForEach-Object {Join-Path $R3EvidenceRoot $_.path})
$protected+=@($profile.sourceNoticeBindings|ForEach-Object {Join-Path $Root $_.path})
$protected+=@((Join-Path $Root $profile.sourceInputs.path),(Join-Path $Root $profile.policyDocument.path))
$before=@{}
foreach($path in $protected){$before[$path]=[ordered]@{sha256=(Get-FileHash -LiteralPath $path).Hash;lastWrite=(Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks}}
$fixtureEntries=@([IO.Directory]::EnumerateFileSystemEntries($TestRoot))
Check 'Read-only design validates at Packaging revision without rewriting historical build revision' {
    $result=Read-S1BinaryReleasePlan $Root $R2EvidenceRoot $R3EvidenceRoot
    if($result.result -cne 'DRAFT_VALIDATED' -or $result.binaryReleaseApproved -or $result.r4BGenerationAuthorized -or
        $result.packageWriterSupported -or $result.publicDistributionAllowed){throw 'Approval boundary'}
    Assert-S1InputSet $fixtureEntries @([IO.Directory]::EnumerateFileSystemEntries($TestRoot))
}
Check '45-file closure changes only two documents; 18 PE and all originals inherit exact identities' {
    Assert-S1InputSet @($r3Profile.files.path) @($profile.files.path)
    $changed=@(foreach($file in $profile.files){$old=@($r3Profile.files|Where-Object path -CEQ $file.path)[0];if($file.sha256 -cne $old.sha256){$file.path}})
    Assert-S1InputSet @('README.md','THIRD-PARTY-NOTICES.md') $changed
    if(@($profile.files|Where-Object kind -CEQ PE).Count -ne 18 -or @($profile.files|Where-Object kind -CEQ license-or-patent).Count -ne 23){throw 'Count'}
}
Check 'Downstream conditions unresolved; mode approval creates no grant or consent UI' {
    if(!$profile.distributionPolicy.ownerApproved -or $profile.microsoftStatus.MS002L -cne 'NEEDS CLARIFICATION' -or
        $profile.distributionPolicy.standaloneEULAPlanned -or $profile.distributionPolicy.clickConsentPlanned -or
        $profile.distributionPolicy.downloadImpliesConsent -or $profile.microsoftStatus.ownerRiskAcceptanceWaivesConditions){throw 'Policy'}
}
Check 'Other-PC report remains owner-reported with no invented environment or exhaustive acceptance' {
    if($profile.otherPC.status -cne 'USER_REPORTED_PASS' -or $profile.otherPC.independentlyVerified -or
        $null -ne $profile.otherPC.OSVersion -or $null -ne $profile.otherPC.runtimeVersion -or
        $profile.otherPC.onDeviceHashesVerified -or $profile.otherPC.exhaustiveFeatureAcceptance){throw 'Report inflation'}
}
Check 'Third-party attribution body is retained verbatim' {
    $old=[IO.File]::ReadAllText((Join-Path $Root packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY.md))
    $text=[IO.File]::ReadAllText((Join-Path $Root packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v1.md))
    $oldBody=$old.Substring($old.IndexOf('clipss2自作部分:'),$old.IndexOf('## Microsoftの未解決範囲')-$old.IndexOf('clipss2自作部分:'))
    $newBody=$text.Substring($text.IndexOf('clipss2自作部分:'),$text.IndexOf('## Microsoftコードの区分と残件')-$text.IndexOf('clipss2自作部分:'))
    if($oldBody -cne $newBody){throw 'Attribution changed'}
}
$original=[IO.File]::ReadAllText((Join-Path $Root packaging/public/profile-s1-binary-release-v1.json))
foreach($change in @('approval','generation','legal-pass','consent','other-pc','PE-rebless')){
    $fixture=Join-Path $TestRoot $change
    $dest=Join-Path $fixture packaging/public/profile-s1-binary-release-v1.json
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
    $text=switch($change){
        approval {$original.Replace('"binaryReleaseApproved": false','"binaryReleaseApproved": true')}
        generation {$original.Replace('"r4BGenerationAuthorized": false','"r4BGenerationAuthorized": true')}
        legal-pass {$original.Replace('"MS002L": "NEEDS CLARIFICATION"','"MS002L": "PASS"')}
        consent {$original.Replace('"downloadImpliesConsent": false','"downloadImpliesConsent": true')}
        other-pc {$original.Replace('"independentlyVerified": false','"independentlyVerified": true')}
        PE-rebless {$original.Replace(@($profile.files|Where-Object kind -CEQ PE)[0].sha256,('A'*64))}
    }
    if($text -ceq $original){throw 'Fixture did not mutate'}
    [IO.File]::WriteAllText($dest,$text,[Text.UTF8Encoding]::new($false))
    Refuse "Pinned new generation rejects $change" {Read-S1BinaryReleaseProfile $fixture}
}
$fakeR2=Join-Path $TestRoot fake-r2
[void][IO.Directory]::CreateDirectory($fakeR2)
[IO.File]::Copy((Join-Path $R2EvidenceRoot identity.json),(Join-Path $fakeR2 identity.json),$false)
[IO.File]::WriteAllText((Join-Path $fakeR2 result.json),'{}')
Refuse 'Altered R2 evidence refused' {Read-S1BinaryReleasePlan $Root $fakeR2 $R3EvidenceRoot}
$fakeR3=Join-Path $TestRoot fake-r3
[void][IO.Directory]::CreateDirectory($fakeR3)
[IO.File]::WriteAllText((Join-Path $fakeR3 result.json),'{}')
Refuse 'Altered R3 evidence refused before reuse' {Read-S1BinaryReleasePlan $Root $R2EvidenceRoot $fakeR3}
Check 'Protected products, ZIP, source anchors, private contracts and evidence unchanged' {
    foreach($path in $before.Keys){
        if((Get-FileHash -LiteralPath $path).Hash -cne $before[$path].sha256 -or
            (Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks -ne $before[$path].lastWrite){throw 'Protected input changed'}
    }
}
$report=[ordered]@{passed=$results.Count;failed=0;scope='R4-A design and hash bindings only; no package generation';tests=@($results.ToArray());binaryReleaseApproved=$false;publicDistributionAllowed=$false}
[IO.File]::WriteAllText((Join-Path $TestRoot results.json),($report|ConvertTo-Json -Depth 16).Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]$report|Select-Object passed,failed,scope,binaryReleaseApproved,publicDistributionAllowed|ConvertTo-Json
