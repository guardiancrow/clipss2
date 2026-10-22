#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
[CmdletBinding()]
param(
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory)][string]$EvidenceRoot,
    [Parameter(Mandatory)][string]$TestRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot ../packaging/S1BinaryPackage.psm1)
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$Root = [IO.Path]::GetFullPath($Root)
$TestRoot = [IO.Path]::GetFullPath($TestRoot)
Assert-S1NoReparse $TestRoot
if (!$TestRoot.StartsWith((Join-Path $Root build) + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $TestRoot)) { throw 'Fixture must be new and under workspace/build' }
[void][IO.Directory]::CreateDirectory($TestRoot)
$results = [Collections.Generic.List[object]]::new()
function Check($Name, [scriptblock]$Action) {
    & $Action
    $results.Add([ordered]@{name=$Name;result='PASS'})
    Write-Host "PASS $Name"
}
function Refuse($Name, [scriptblock]$Action, [string]$Pattern) {
    $errorMessage = $null
    try { $null = & $Action } catch { $errorMessage = $_.Exception.Message }
    if (!$errorMessage -or $errorMessage -notmatch $Pattern) { throw "Expected refusal: $Name; actual=$errorMessage" }
    $results.Add([ordered]@{name=$Name;result='PASS';refusal=$errorMessage})
    Write-Host "PASS $Name"
}
function Copy-Payload($Name) {
    $dest = Join-Path $TestRoot $Name
    [void][IO.Directory]::CreateDirectory($dest)
    foreach ($file in $profile.files) {
        $path = Join-Path $dest $file.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::Copy((Join-Path $good.payload $file.path),$path,$false)
    }
    return $dest
}
function Mutate-Byte($Path) {
    $stream = [IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite)
    try { $value = $stream.ReadByte(); $stream.Position=0; $stream.WriteByte($value -bxor 1) } finally { $stream.Dispose() }
}
function New-BadZip($Name, $Change) {
    $path = Join-Path $TestRoot ($Name + '.zip')
    $stream = [IO.File]::Open($path,[IO.FileMode]::CreateNew)
    $archive = [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try {
        for ($i=0; $i -lt $profile.files.Count; $i++) {
            $file=$profile.files[$i]; $entryName=$file.path
            if ($Change -eq 'missing' -and $file.path -ceq 'licenses/jpegli/PATENTS') { continue }
            if ($i -eq 0) {
                switch ($Change) {
                    traversal { $entryName='../escape' }
                    absolute { $entryName='C:/escape' }
                    backslash { $entryName='licenses\escape' }
                    duplicate { $entryName=$profile.files[1].path.ToUpperInvariant() }
                }
            }
            $entry=$archive.CreateEntry($entryName,[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime=[DateTimeOffset]::Parse($profile.zipEntryDate)
            $entry.ExternalAttributes=32
            if ($i -eq 0 -and $Change -eq 'symlink') { $entry.ExternalAttributes=-1577123840 }
            $data=[IO.File]::ReadAllBytes((Join-Path $good.payload $file.path))
            if ($Change -eq 'content' -and $file.kind -ceq 'PE') { $data[0]=$data[0] -bxor 1 }
            $output=$entry.Open()
            try { $output.Write($data,0,$data.Length) } finally { $output.Dispose() }
        }
        if ($Change -eq 'extra') { $null=$archive.CreateEntry('settings.conf') }
    } finally { $archive.Dispose(); $stream.Dispose() }
    return $path
}
$profile = Read-S1BinaryProfile $Root
$good = @{root=(Join-Path $TestRoot good);payload=(Join-Path $TestRoot good/payload)}
Check 'PlanOnly: no fixture writes, no approval inherited' {
    $before=@([IO.Directory]::EnumerateFileSystemEntries($TestRoot))
    $plan=Read-S1BinaryPlan $Root $EvidenceRoot
    if ($plan.binaryReleaseApproved -or $plan.publicDistributionAllowed -or $plan.PE -ne 18 -or $plan.files -ne 45) { throw 'Plan contract' }
    Assert-S1InputSet $before @([IO.Directory]::EnumerateFileSystemEntries($TestRoot))
}
Check 'Create: 18 exact PE, 23 originals, closed 45 files; no approval' {
    $receipt=New-S1BinaryCandidate $Root $EvidenceRoot $good.root
    if ($receipt.binaryReleaseApproved -or $receipt.publicDistributionAllowed -or $receipt.files -ne 45) { throw 'Receipt contract' }
    $actual=Read-S1StrictJson (Join-Path $good.root acceptance.json)
    if ($actual.zip.sha256 -cne (Get-FileHash -LiteralPath (Join-Path $good.root $profile.zipName)).Hash) { throw 'ZIP identity' }
    $text=(($actual.inventory|ForEach-Object {"{0}`t{1}`t{2}" -f $_.path,$_.size,$_.sha256}) -join "`n")+"`n"
    if ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($text))) -cne $actual.inventorySha256) { throw 'Inventory digest' }
}
Check 'ValidateTree and ValidateZip: isolated expansion' {
    $null=Test-S1BinaryTree $Root $EvidenceRoot (Join-Path $good.root expanded)
    $null=Test-S1BinaryZip $Root $EvidenceRoot (Join-Path $good.root $profile.zipName)
}
Check 'Portable marker: exact ASCII and LF' {
    $bytes=[IO.File]::ReadAllBytes((Join-Path $good.payload clipss2.portable))
    if ($bytes.Length -ne 20 -or [Text.Encoding]::ASCII.GetString($bytes) -cne "clipss2-portable-v1`n") { throw 'Marker' }
}
Check 'New STL license: LF text reconstructs exact upstream CRLF original' {
    $text=[IO.File]::ReadAllText((Join-Path $good.payload licenses/msvc-stl/LICENSE.txt))
    if ($text.Contains("`r")) { throw 'STL is not LF' }
    $bytes=[Text.Encoding]::UTF8.GetBytes($text.Replace("`n","`r`n"))
    if ($bytes.Length -ne $profile.stlLicenseOrigin.upstreamSize -or
        [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)) -cne $profile.stlLicenseOrigin.upstreamSha256) { throw 'Original text changed' }
}
$empty=Join-Path $TestRoot empty
[void][IO.Directory]::CreateDirectory($empty)
Refuse 'Existing empty output refused' { New-S1BinaryCandidate $Root $EvidenceRoot $empty } 'S1:Binary:ExistingOutput'
$sentinel=Join-Path $empty sentinel
[IO.File]::WriteAllText($sentinel,'owned-existing-data')
Refuse 'Existing unknown output refused' { New-S1BinaryCandidate $Root $EvidenceRoot $empty } 'S1:Binary:ExistingOutput'
Check 'Existing data preserved; no acceptance receipt' {
    if ([IO.File]::ReadAllText($sentinel) -cne 'owned-existing-data' -or (Test-Path -LiteralPath (Join-Path $empty acceptance.json))) { throw 'Overwrite' }
}
Refuse 'Outside build output refused before writing' { New-S1BinaryCandidate $Root $EvidenceRoot (Join-Path $Root forbidden-r3-output) } 'S1:Binary:OutputScope'
foreach ($change in @('approval','hash-rebless','duplicate-json')) {
    $fixture=Join-Path $TestRoot ('profile-'+$change)
    $path=Join-Path $fixture packaging/public/profile-s1-binary-private-v1.json
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    $text=[IO.File]::ReadAllText((Join-Path $Root packaging/public/profile-s1-binary-private-v1.json))
    switch ($change) {
        approval { $text=$text.Replace('"binaryReleaseApproved": false','"binaryReleaseApproved": true') }
        hash-rebless { $text=$text.Replace($profile.files[0].sha256,('A'*64)) }
        duplicate-json { $text=$text.Replace('"schemaVersion": 1,','"schemaVersion": 1, "schemaVersion": 1,') }
    }
    [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    Refuse "Pinned profile refuses $change" { Read-S1BinaryProfile $fixture } 'S1:RawIdentity'
}
foreach ($change in @('identity','approval')) {
    $fixture=Join-Path $TestRoot ('evidence-'+$change)
    foreach ($binding in $profile.r2Bindings) {
        $path=Join-Path $fixture $binding.path
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::Copy((Join-Path $EvidenceRoot $binding.path),$path,$false)
    }
    if ($change -eq 'identity') { Mutate-Byte (Join-Path $fixture identity.json) }
    else {
        $path=Join-Path $fixture result.json
        [IO.File]::WriteAllText($path,[IO.File]::ReadAllText($path).Replace('"binaryReleaseApproved": false','"binaryReleaseApproved": true'))
    }
    $output=Join-Path $TestRoot ('rejected-'+$change)
    Refuse "Changed R2 $change refused before output creation" { New-S1BinaryCandidate $Root $fixture $output } 'S1:RawIdentity'
    if (Test-Path -LiteralPath $output) { throw 'Failed admission wrote output' }
}
foreach ($extra in @('settings.conf','sample.png','product.pdb','audit.log','PRIVATE/notes.md')) {
    $fixture=Copy-Payload ('extra-'+$extra.Replace('/','-'))
    $path=Join-Path $fixture $extra
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path,'synthetic fixture')
    Refuse "Closed tree refuses $extra" { Test-S1BinaryTree $Root $EvidenceRoot $fixture } 'S1:ScopeFiles'
}
$fixture=Copy-Payload 'empty-dir'
[void][IO.Directory]::CreateDirectory((Join-Path $fixture unused))
Refuse 'Empty extra directory refused' { Test-S1BinaryTree $Root $EvidenceRoot $fixture } 'S1:ScopeDirectories'
foreach ($path in @('clipss2.exe','licenses/jpegli/PATENTS','clipss2.portable')) {
    $fixture=Copy-Payload ('modified-'+$path.Replace('/','-'))
    Mutate-Byte (Join-Path $fixture $path)
    Refuse "Modified $path refused" { Test-S1BinaryTree $Root $EvidenceRoot $fixture } 'S1:RawIdentity'
}
$fixture=Copy-Payload 'missing-license'
[IO.File]::Move((Join-Path $fixture licenses/jpegli/LICENSE),(Join-Path $TestRoot retained-license))
Refuse 'Missing original refused' { Test-S1BinaryTree $Root $EvidenceRoot $fixture } 'S1:ScopeFiles'
$fixture=Copy-Payload 'case-alias'
[IO.File]::Move((Join-Path $fixture clipss2.exe),(Join-Path $fixture CLIPSS2.EXE))
Refuse 'Wrong path case refused' { Test-S1BinaryTree $Root $EvidenceRoot $fixture } 'S1:ScopeFiles'
$fixture=Copy-Payload 'reparse'
$target=Join-Path $TestRoot owned-link-target
[void][IO.Directory]::CreateDirectory($target)
$null=New-Item -ItemType Junction -Path (Join-Path $fixture unexpected-link) -Target $target
Refuse 'Reparse tree refused' { Test-S1BinaryTree $Root $EvidenceRoot $fixture } 'S1:ReparsePoint'
Refuse 'Reparse output refused' { New-S1BinaryCandidate $Root $EvidenceRoot (Join-Path $fixture unexpected-link/output) } 'S1:ReparsePoint'
foreach ($change in @('traversal','absolute','backslash','duplicate','missing','extra','symlink','content')) {
    $path=New-BadZip ('zip-'+$change) $change
    Refuse "ZIP refuses $change" { Test-S1BinaryZip $Root $EvidenceRoot $path } 'S1:(Binary:Zip|RelativePath)'
}
$bytes=[IO.File]::ReadAllBytes((Join-Path $good.root $profile.zipName))
$path=Join-Path $TestRoot zip-comment.zip
$withComment=[byte[]]::new($bytes.Length+1)
[Array]::Copy($bytes,$withComment,$bytes.Length)
$withComment[$bytes.Length-2]=1; $withComment[$bytes.Length]=65
[IO.File]::WriteAllBytes($path,$withComment)
Refuse 'ZIP comment/trailing data refused' { Test-S1BinaryZip $Root $EvidenceRoot $path } 'S1:Binary:ZipLayout'
Check 'Partial failure retains payload/ZIP but publishes no acceptance receipt' {
    # Process-local failure injection at the ZIP admission boundary. Production
    # code and all input/evidence bytes remain unchanged.
    $module=Get-Module S1BinaryPackage
    $original=& $module { (Get-Command Read-S1BinaryZip).ScriptBlock }
    $partial=Join-Path $TestRoot partial-unadmitted
    try {
        & $module { function script:Read-S1BinaryZip { throw 'S1:Binary:InjectedZipAdmissionFailure' } }
        Refuse 'Injected ZIP admission failure stops creation' { New-S1BinaryCandidate $Root $EvidenceRoot $partial } 'S1:Binary:InjectedZipAdmissionFailure'
    } finally { & $module { param($Body) Set-Item Function:script:Read-S1BinaryZip $Body } $original }
    if (!(Test-Path -LiteralPath (Join-Path $partial payload/clipss2.exe)) -or
        !(Test-Path -LiteralPath (Join-Path $partial $profile.zipName)) -or
        (Test-Path -LiteralPath (Join-Path $partial acceptance.json)) -or
        (Test-Path -LiteralPath (Join-Path $partial expanded))) { throw 'Partial acceptance' }
    $owner=Read-S1StrictJson (Join-Path $partial owner.json)
    if ($owner.reuseAllowed -or $owner.binaryReleaseApproved) { throw 'Owner admission' }
}
Check 'Original 18 PE/R2 evidence and immutable NOTICE bindings still match' { $null=Read-S1BinaryPlan $Root $EvidenceRoot }
$report=[ordered]@{passed=$results.Count;failed=0;scope='Packaging only; no PE/regression rebuild or rerun';tests=@($results.ToArray());binaryReleaseApproved=$false}
[IO.File]::WriteAllText((Join-Path $TestRoot results.json),($report|ConvertTo-Json -Depth 16)+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]$report|Select-Object passed,failed,scope,binaryReleaseApproved|ConvertTo-Json
