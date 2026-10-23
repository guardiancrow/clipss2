#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$ZipPath,
    [Parameter(Mandatory)][string]$TestRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$Root=[IO.Path]::GetFullPath($Root);$TestRoot=[IO.Path]::GetFullPath($TestRoot)
Assert-S1NoReparse $TestRoot
if(!$TestRoot.StartsWith((Join-Path $Root build/phase3e-release-notes-tests-),[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $TestRoot)){throw 'New owned test root required'}
[void][IO.Directory]::CreateDirectory($TestRoot)
$script=Join-Path $Root tools/check_v4_release_notes.ps1
$notes=Join-Path $Root docs/RELEASE-NOTES-0.1.0-V4.md
$original=[IO.File]::ReadAllText($notes)
$results=[Collections.Generic.List[object]]::new()
$before=@{};foreach($p in @($notes,$ZipPath,(Join-Path $Root packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt))){$before[$p]=@{hash=(Get-FileHash -LiteralPath $p).Hash;write=(Get-Item -LiteralPath $p).LastWriteTimeUtc.Ticks}}
function Check($Name,[scriptblock]$Action){& $Action;$results.Add([ordered]@{name=$Name;result='PASS'});Write-Host "PASS $Name"}
function Refuse($Name,[scriptblock]$Action,$Pattern){$message=$null;try{$null=& $Action}catch{$message=$_.Exception.Message};if(!$message -or $message -notmatch $Pattern){throw "Expected refusal: $Name; actual=$message"};$results.Add([ordered]@{name=$Name;result='PASS';refusal=$message});Write-Host "PASS $Name"}
function New-Fixture($Name,$Text){$path=Join-Path $TestRoot $Name;$stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew);try{$stream.Write([Text.UTF8Encoding]::new($false).GetBytes($Text))}finally{$stream.Dispose()};return $path}
Check 'Draft accepts exactly two pending replacements without publication approval' {
    $r=& $script -Root $Root -ZipPath $ZipPath|ConvertFrom-Json
    if($r.result -cne 'REVIEW_DRAFT_VALID' -or $r.replacementCount -ne 2 -or $r.binaryReleaseApproved -or $r.publicDistributionAllowed -or $r.sdk2ApprovalGranted){throw 'Draft boundary'}
}
Refuse 'Unresolved final body refused' {& $script -Mode CheckFinal -Root $Root -ZipPath $ZipPath} 'ReleaseText:UnresolvedPlaceholder'
$sha=& git --no-optional-locks -C $Root rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $sha -cnotmatch '^[a-f0-9]{40}$'){throw 'Local commit unavailable'}
$permalink='https://github.com/guardiancrow/clipss2/blob/'+$sha+'/packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt'
$rendered=$original.Replace('{{MICROSOFT_TERMS_PERMALINK}}',$permalink).Replace('{{SOURCE_ARCHIVE_COMMIT}}',$sha)
$final=New-Fixture final-body.md $rendered
Check 'Locally rendered text validates while public commit verification remains false' {
    $r=& $script -Mode CheckFinal -Root $Root -ZipPath $ZipPath -NotesPath $final -SourceCommit $sha|ConvertFrom-Json
    if($r.result -cne 'FINAL_TEXT_VALID_ONLY' -or $r.publicTermsCommitVerified -or !$r.requiresRemotePermalinkVerification -or $r.publicDistributionAllowed -or $r.allowedBinaryAssets.Count -ne 1){throw 'URL shape became publication proof'}
}
$bad=New-Fixture old-hash.md ($original.Replace('3803495B4798C6B7D6B30BC0A0E08495FC275DB4E0A576A78CCD60BEBBF0FECC','B36CEFBDD2D7C00CE0F5FF17278365A977547C2655AB5D5CFDDC3B263025E5B2'))
Refuse 'v3 hash cannot replace v4 hash' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'ReleaseText:MissingRequiredText'
$bad=New-Fixture extra-old-hash.md ($original+"`nB36CEFBDD2D7C00CE0F5FF17278365A977547C2655AB5D5CFDDC3B263025E5B2`n")
Refuse 'v3 hash cannot coexist in the body' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'ReleaseText:OldIdentity'
$bad=New-Fixture no-agreement.md ($original.Replace('The distributor requires recipients and redistributors of the Covered SDK Code to agree','Notice only'))
Refuse 'Agreement request cannot become a notice only' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'ReleaseText:PreDownloadNotice'
$bad=New-Fixture wrong-product-revision.md ($original.Replace('93545d458da33cef397fb77458336ea06339dcb6',$sha))
Refuse 'Packaging commit cannot replace product build revision' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'ReleaseText:MissingRequiredText'
$bad=New-Fixture branch-permalink.md ($rendered.Replace('/blob/'+$sha+'/','/blob/main/'))
Refuse 'Branch link cannot be an immutable terms permalink' {& $script -Mode CheckFinal -Root $Root -ZipPath $ZipPath -NotesPath $bad -SourceCommit $sha} 'ReleaseText:TermsPermalink'
$bad=New-Fixture unknown-token.md ($original+"`n{{UNKNOWN}}`n")
Refuse 'Unknown replacement refused' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'S1:InputSet'
$bad=New-Fixture private-asset-link.md ($original+"`n[internal receipt](acceptance.json)`n")
Refuse 'Internal receipt link refused' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'ReleaseText:UnexpectedLink'
$bad=New-Fixture wrong-source-commit.md ($rendered.Replace('`'+$sha+'`','`'+('0'*40)+'`'))
Refuse 'Source archive commit display must match terms target' {& $script -Mode CheckFinal -Root $Root -ZipPath $ZipPath -NotesPath $bad -SourceCommit $sha} 'ReleaseText:SourceCommitDisplay'
$bad=New-Fixture unexpected-real-url.md ($original.Replace('{{MICROSOFT_TERMS_PERMALINK}}',$permalink))
Refuse 'Draft cannot silently substitute a not-verified public URL' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'S1:InputSet'
$bad=New-Fixture crlf.md ($original.Replace("`n","`r`n"))
Refuse 'Unexpected CRLF refused' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'ReleaseText:Encoding'
$bad=New-Fixture bom.md ([string][char]0xFEFF+$original)
Refuse 'Unexpected BOM refused using ordinal comparison' {& $script -Root $Root -ZipPath $ZipPath -NotesPath $bad} 'ReleaseText:Encoding'
$badZip=Join-Path $TestRoot clipss2-0.1.0-windows-x64-portable.zip
[IO.File]::WriteAllBytes($badZip,[byte[]]@(80,75,0,0))
Refuse 'Unadmitted asset bytes refused' {& $script -Root $Root -ZipPath $badZip} 'S1:RawIdentity'
Check 'Notes, accepted ZIP and terms retain bytes and write times' {
    foreach($p in $before.Keys){if((Get-FileHash -LiteralPath $p).Hash -cne $before[$p].hash -or (Get-Item -LiteralPath $p).LastWriteTimeUtc.Ticks -ne $before[$p].write){throw 'Read-only inputs changed'}}
}
$report=[ordered]@{passed=$results.Count;failed=0;scope='Phase 3-E new Release text checks only';tests=@($results.ToArray());binaryReleaseApproved=$false;publicDistributionAllowed=$false}
[IO.File]::WriteAllText((Join-Path $TestRoot results.json),($report|ConvertTo-Json -Depth 12).Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]$report|Select-Object passed,failed,scope,binaryReleaseApproved,publicDistributionAllowed|ConvertTo-Json
