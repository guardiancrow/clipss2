#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Text/asset checks only. This never approves SDK-2/G3 or creates a Release.
[CmdletBinding()]
param(
    [ValidateSet('ReviewDraft','CheckFinal')][string]$Mode='ReviewDraft',
    [string]$Root=(Split-Path $PSScriptRoot -Parent),
    [string]$NotesPath,
    [Parameter(Mandatory)][string]$ZipPath,
    [string]$SourceCommit
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot S1SourceContracts.psm1)
$Root=[IO.Path]::GetFullPath($Root)
if(!$NotesPath){$NotesPath=Join-Path $Root docs/RELEASE-NOTES-0.1.0-V4.md}
$NotesPath=[IO.Path]::GetFullPath($NotesPath);$ZipPath=[IO.Path]::GetFullPath($ZipPath)
Assert-S1NoReparse $NotesPath
Assert-S1Identity $ZipPath 7968423 '3803495B4798C6B7D6B30BC0A0E08495FC275DB4E0A576A78CCD60BEBBF0FECC'
if([IO.Path]::GetFileName($ZipPath) -cne 'clipss2-0.1.0-windows-x64-portable.zip'){throw 'ReleaseText:AssetName'}
$termsPath=Resolve-S1Path $Root 'packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt'
Assert-S1Identity $termsPath 8447 'F19F7CC511F32BA218EED0B93ED02CB058A79F3F3605499649369BA6FFBF4F20'
$bytes=[IO.File]::ReadAllBytes($NotesPath)
$text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
if($text.Contains("`r") -or $text.StartsWith([string][char]0xFEFF,[StringComparison]::Ordinal)){throw 'ReleaseText:Encoding'}
foreach($literal in @('clipss2 v0.1.0','Windows x64 Portable','Microsoft Visual C++ v14 Runtime','THIRD-PARTY-NOTICES.md','MIT License','コード署名を付していません','ユーザーから報告','未記録','7,968,423 bytes','3803495B4798C6B7D6B30BC0A0E08495FC275DB4E0A576A78CCD60BEBBF0FECC','93545d458da33cef397fb77458336ea06339dcb6','Source archive','clipss2-0.1.0-windows-x64-portable.zip')){
    if(!$text.Contains($literal)){throw 'ReleaseText:MissingRequiredText'}
}
foreach($old in @('B36CEFBDD2D7C00CE0F5FF17278365A977547C2655AB5D5CFDDC3B263025E5B2','5FDCB141E825E7FFADDFFA6EEC16B5BFEBB7CF65C08C690DD98BFA6A83D03B0A','7,967,972 bytes','7,964,729 bytes')){if($text.Contains($old)){throw 'ReleaseText:OldIdentity'}}
$notice='Before downloading the binary package, review '
$instruction='The distributor requires recipients and redistributors of the Covered SDK Code to agree'
if($text.IndexOf($notice,[StringComparison]::Ordinal) -lt 0 -or !$text.Contains($instruction) -or $text.IndexOf($notice,[StringComparison]::Ordinal) -gt $text.IndexOf('## 必要環境・導入',[StringComparison]::Ordinal)){throw 'ReleaseText:PreDownloadNotice'}
$tokens=@([regex]::Matches($text,'\{\{[^}\r\n]+\}\}')|ForEach-Object Value)
$termsToken='{{MICROSOFT_TERMS_PERMALINK}}';$commitToken='{{SOURCE_ARCHIVE_COMMIT}}'
if($Mode -ceq 'ReviewDraft'){
    if($SourceCommit){throw 'ReleaseText:UnexpectedSourceCommit'}
    Assert-S1InputSet @($termsToken,$commitToken) $tokens
    if(@($tokens|Where-Object {$_ -ceq $termsToken}).Count -ne 1 -or @($tokens|Where-Object {$_ -ceq $commitToken}).Count -ne 1){throw 'ReleaseText:PlaceholderCount'}
    $termsTarget=$termsToken
}else{
    if($tokens.Count){throw 'ReleaseText:UnresolvedPlaceholder'}
    if($SourceCommit -cnotmatch '^[a-f0-9]{40}$'){throw 'ReleaseText:SourceCommit'}
    $termsTarget='https://github.com/guardiancrow/clipss2/blob/'+$SourceCommit+'/packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt'
    if(!$text.Contains('`'+$SourceCommit+'`')){throw 'ReleaseText:SourceCommitDisplay'}
}
if(!$text.Contains('[MICROSOFT-COMPONENT-TERMS.txt]('+$termsTarget+')')){throw 'ReleaseText:TermsPermalink'}
$allowed=@($termsTarget,'https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist')
foreach($link in [regex]::Matches($text,'\]\(([^)]+)\)')){if($link.Groups[1].Value -cnotin $allowed){throw 'ReleaseText:UnexpectedLink'}}
# No upload, rendering, approval update or network lookup. A valid URL shape is not proof it exists.
[ordered]@{
    result=if($Mode -ceq 'ReviewDraft'){'REVIEW_DRAFT_VALID'}else{'FINAL_TEXT_VALID_ONLY'}
    zipSize=7968423;zipSha256='3803495B4798C6B7D6B30BC0A0E08495FC275DB4E0A576A78CCD60BEBBF0FECC'
    allowedBinaryAssets=@('clipss2-0.1.0-windows-x64-portable.zip')
    replacementCount=$tokens.Count;sourceCommit=$SourceCommit
    publicTermsCommitVerified=$false;requiresRemotePermalinkVerification=$true
    sdk2ApprovalGranted=$false;binaryReleaseApproved=$false;publicDistributionAllowed=$false
}|ConvertTo-Json -Depth 4
