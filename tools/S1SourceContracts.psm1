#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# First-party independent implementation. Read-only boundary checks follow the
# intent of ShippingPrivacyPaths/ShippingPrivacy and strict NOTICE validation;
# no legacy module, manifest, anchor, writer, compiler, or network is imported.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:OriginSha = '2E96789787E73F37A8278842ED5258CE71976D08935D644EFDDA8FBA8F07D563'
$script:ManifestSha = '7D4BC1700562893F34721C9AECE02C5D42672C21EB9071341BD17107DD53777D'
$script:CheckoutPaths = @('.', 'third_party/highway', 'third_party/skcms', 'third_party/libjpeg-turbo')
$script:CheckoutUris = @('https://github.com/google/jpegli.git', 'https://github.com/google/highway.git', 'https://skia.googlesource.com/skcms', 'https://github.com/libjpeg-turbo/libjpeg-turbo.git')
$script:Pins = @('031a0077f5799a6041004267fc12b956c1f52a20','271a9a0ed9de1232d9117f1572c3fe28f8542ec1','96d9171c94b937a1b5f0293de7309ac16311b722','8ecba3647edb6dd940463fedf38ca33a8e2a73d1')
$script:GitlinkPaths = @('testdata','third_party/googletest','third_party/highway','third_party/lcms','third_party/libjpeg-turbo','third_party/libpng','third_party/sjpeg','third_party/skcms','third_party/zlib')
$script:GitlinkOids = @('ff8d743aaba05b3014f17e5475e576242fa979fc','6910c9d9165801d8827d628cb72eb7ea9dd538c5','271a9a0ed9de1232d9117f1572c3fe28f8542ec1','5176347635785e53ee5cee92328f76fda766ecc6','8ecba3647edb6dd940463fedf38ca33a8e2a73d1','872555f4ba910252783af1507f9e7fe1653be252','94e0df6d0f8b44228de5be0ff35efb9f946a13c9','96d9171c94b937a1b5f0293de7309ac16311b722','51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf')

function Fail-S1($Code) { throw [IO.InvalidDataException]::new('S1:' + $Code) }
function Assert-S1String($Value) {
    if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value)) { Fail-S1 'ScalarString' }
}
function Assert-S1Object($Value, $Fields) {
    if ($Value -isnot [Collections.IDictionary]) { Fail-S1 'ObjectType' }
    if ($Value.Count -ne $Fields.Count) { Fail-S1 'Fields' }
    foreach ($key in $Value.Keys) {
        if ($key -isnot [string] -or $key -cnotin $Fields) { Fail-S1 'Fields' }
    }
}
function Assert-S1Array($Value, $Count) {
    if ($Value -isnot [array] -or $Value.Count -ne $Count) { Fail-S1 'ArrayShape' }
}
function Assert-S1Integer($Value, $Expected) {
    if (($Value -isnot [int] -and $Value -isnot [long]) -or $Value -ne $Expected) { Fail-S1 'IntegerValue' }
}
function Assert-S1ExactString($Value, $Expected, $Code = 'StringValue') {
    Assert-S1String $Value
    if ($Value -cne $Expected) { Fail-S1 $Code }
}
function ConvertFrom-S1Utf8($Bytes) {
    if ($Bytes -isnot [byte[]]) { Fail-S1 'BytesType' }
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 239 -and $Bytes[1] -eq 187 -and $Bytes[2] -eq 191) { Fail-S1 'Utf8Bom' }
    try { return [Text.UTF8Encoding]::new($false, $true).GetString($Bytes) }
    catch { Fail-S1 'Utf8Encoding' }
}
function Assert-S1JsonKeys($Element) {
    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($property in $Element.EnumerateObject()) {
            if (!$keys.Add($property.Name)) { Fail-S1 'DuplicateJsonKey' }
            Assert-S1JsonKeys $property.Value
        }
    } elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        foreach ($item in $Element.EnumerateArray()) { Assert-S1JsonKeys $item }
    }
}
function ConvertFrom-S1StrictJson($Bytes) {
    $text = ConvertFrom-S1Utf8 $Bytes
    $options = [Text.Json.JsonDocumentOptions]::new()
    $options.MaxDepth = 32
    $options.AllowTrailingCommas = $false
    $options.CommentHandling = [Text.Json.JsonCommentHandling]::Disallow
    try { $document = [Text.Json.JsonDocument]::Parse([string]$text, $options) }
    catch { Fail-S1 'JsonSyntax' }
    try {
        Assert-S1JsonKeys $document.RootElement
        if ($document.RootElement.ValueKind -ne [Text.Json.JsonValueKind]::Object) { Fail-S1 'JsonRootObject' }
    } finally { $document.Dispose() }
    try { $result = ConvertFrom-Json -InputObject $text -AsHashtable -Depth 32 }
    catch { Fail-S1 'JsonSyntax' }
    return ,$result
}
function Assert-S1RelativePath($Value) {
    Assert-S1String $Value
    if ($Value -cne $Value.Normalize([Text.NormalizationForm]::FormC) -or
        $Value -match '[\x00-\x1f\x7f\\:*?"<>|]' -or $Value.StartsWith('/') -or $Value.EndsWith('/')) { Fail-S1 'RelativePath' }
    foreach ($part in $Value.Split('/')) {
        if (!$part -or $part -in @('.','..') -or $part -match '[. ]$|^ ' -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9¹²³]|LPT[0-9¹²³])(?:\.|$)') { Fail-S1 'RelativePath' }
    }
}
function Get-S1Absolute($Value) {
    Assert-S1String $Value
    if (![IO.Path]::IsPathFullyQualified($Value) -or $Value -match '^\\\\' -or $Value -match '^//') { Fail-S1 'AbsoluteRoot' }
    return [IO.Path]::GetFullPath($Value).TrimEnd([char[]]@('\','/'))
}
function Assert-S1NoReparse($Path) {
    $cursor = Get-S1Absolute $Path
    while ($cursor) {
        try {
            $attributes = [IO.File]::GetAttributes($cursor)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Fail-S1 'ReparsePoint' }
        } catch [IO.FileNotFoundException] {} catch [IO.DirectoryNotFoundException] {}
        $parentPath = [IO.Path]::GetDirectoryName($cursor)
        if (!$parentPath -or $parentPath -eq $cursor) { break }
        $cursor = $parentPath
    }
}
function Resolve-S1Path($Root, $Relative) {
    Assert-S1RelativePath $Relative
    $rootPath = Get-S1Absolute $Root
    Assert-S1NoReparse $rootPath
    if (![IO.Directory]::Exists($rootPath)) { Fail-S1 'RootMissing' }
    $current = $rootPath
    foreach ($part in $Relative.Split('/')) {
        # Enumerating each existing component rejects Windows case/8.3 aliases.
        if ([IO.Directory]::Exists($current)) {
            $aliases = @([IO.DirectoryInfo]::new($current).EnumerateFileSystemInfos() | Where-Object { [string]::Equals($_.Name,$part,[StringComparison]::OrdinalIgnoreCase) })
            if ($aliases.Count -gt 1 -or ($aliases.Count -eq 1 -and $aliases[0].Name -cne $part)) { Fail-S1 'PathCaseAlias' }
        }
        $current = [IO.Path]::GetFullPath((Join-Path $current $part))
        Assert-S1NoReparse $current
        if (([IO.File]::Exists($current) -or [IO.Directory]::Exists($current)) -and $aliases.Count -eq 0) { Fail-S1 'PathCaseAlias' }
    }
    if (!$current.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::Ordinal)) { Fail-S1 'RootEscape' }
    # Short-name aliases which did not match an enumerated long name are rejected.
    if ([IO.File]::Exists($current) -or [IO.Directory]::Exists($current)) {
        $actualName = [IO.FileInfo]::new($current).Name
        $last = $Relative.Split('/')[-1]
        $parent = [IO.Path]::GetDirectoryName($current)
        if (!@([IO.DirectoryInfo]::new($parent).EnumerateFileSystemInfos() | Where-Object Name -CEQ $last).Count -or $actualName -cne $last) { Fail-S1 'PathCaseAlias' }
    }
    return $current
}
function Assert-S1Identity($Path, $Size, $Sha256) {
    Assert-S1String $Path
    Assert-S1String $Sha256
    if (($Size -isnot [int] -and $Size -isnot [long]) -or $Size -lt 0 -or $Sha256 -cnotmatch '^[A-F0-9]{64}$') { Fail-S1 'IdentityType' }
    Assert-S1NoReparse $Path
    if (![IO.File]::Exists($Path)) { Fail-S1 'FileMissing' }
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ne $Size -or [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)) -cne $Sha256) { Fail-S1 'RawIdentity' }
}
function Read-S1StrictJson($Path) {
    Assert-S1String $Path
    Assert-S1NoReparse $Path
    if (![IO.File]::Exists($Path)) { Fail-S1 'FileMissing' }
    return ,(ConvertFrom-S1StrictJson ([IO.File]::ReadAllBytes($Path)))
}
function Assert-S1Gitlinks($Value) {
    Assert-S1Array $Value 9
    for ($i=0; $i -lt 9; $i++) {
        Assert-S1Object $Value[$i] @('mode','type','oid','path')
        Assert-S1ExactString $Value[$i].mode '160000' 'Gitlinks'
        Assert-S1ExactString $Value[$i].type 'commit' 'Gitlinks'
        Assert-S1ExactString $Value[$i].oid $script:GitlinkOids[$i] 'Gitlinks'
        Assert-S1ExactString $Value[$i].path $script:GitlinkPaths[$i] 'Gitlinks'
    }
}
function Assert-S1OriginSchema($Value) {
    Assert-S1Object $Value @('schemaVersion','contractId','sourceScopeId','checkouts','gitlinks','manifest')
    Assert-S1Integer $Value.schemaVersion 1
    Assert-S1ExactString $Value.contractId 'clipss2-managed-origin-s1-v1'
    Assert-S1ExactString $Value.sourceScopeId 'clipss2-source-s1-v1' 'Scope'
    Assert-S1Array $Value.checkouts 4
    for ($i=0; $i -lt 4; $i++) {
        $entry=$Value.checkouts[$i]
        Assert-S1Object $entry @('path','uri','revision')
        Assert-S1ExactString $entry.path $script:CheckoutPaths[$i] 'Checkout'
        Assert-S1ExactString $entry.uri $script:CheckoutUris[$i] 'Checkout'
        Assert-S1ExactString $entry.revision $script:Pins[$i] 'Checkout'
    }
    Assert-S1Gitlinks $Value.gitlinks
    $manifest=$Value.manifest
    Assert-S1Object $manifest @('path','size','sha256','rowCount','columns')
    Assert-S1ExactString $manifest.path 'cmake/privacy/managed-origin-s1-v1.tsv' 'ManifestPath'
    Assert-S1Integer $manifest.size 231616
    Assert-S1ExactString $manifest.sha256 $script:ManifestSha 'ManifestHash'
    Assert-S1Integer $manifest.rowCount 1481
    Assert-S1Array $manifest.columns 4
    $columns=@('originalSha256','mode','gitBlob','path')
    for ($i=0; $i -lt 4; $i++) { Assert-S1ExactString $manifest.columns[$i] $columns[$i] 'Columns' }
}
function ConvertFrom-S1OriginManifest($Bytes) {
    $text=ConvertFrom-S1Utf8 $Bytes
    if ($text.Contains([char]13) -or !$text.EndsWith("`n",[StringComparison]::Ordinal)) { Fail-S1 'TsvLf' }
    $lines=$text.Substring(0,$text.Length-1).Split([char]10)
    if ($lines.Count -ne 1481) { Fail-S1 'RowCount' }
    $rows=[Collections.Generic.List[object]]::new()
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $previous=$null
    foreach ($line in $lines) {
        $parts=$line.Split([char]9)
        if ($parts.Count -ne 4) { Fail-S1 'TsvColumns' }
        if ($parts[0] -cnotmatch '^[A-F0-9]{64}$') { Fail-S1 'OriginalSha' }
        if ($parts[1] -cnotmatch '^100(?:644|755)$') { Fail-S1 'GitMode' }
        if ($parts[2] -cnotmatch '^[a-f0-9]{40}$') { Fail-S1 'GitBlob' }
        Assert-S1RelativePath $parts[3]
        if (!$seen.Add($parts[3])) { Fail-S1 'PathCollision' }
        if ($null -ne $previous -and [StringComparer]::Ordinal.Compare($previous,$parts[3]) -ge 0) { Fail-S1 'PathOrder' }
        $previous=$parts[3]
        $rows.Add([pscustomobject]@{originalSha256=$parts[0];mode=$parts[1];gitBlob=$parts[2];path=$parts[3]})
    }
    if ($Bytes.Length -ne 231616 -or [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)) -cne $script:ManifestSha) { Fail-S1 'ManifestRawIdentity' }
    return ,$rows.ToArray()
}
function Read-S1OriginContract($Root) {
    $jsonPath=Resolve-S1Path $Root 'cmake/privacy/managed-origin-s1-v1.json'
    $value=Read-S1StrictJson $jsonPath
    Assert-S1OriginSchema $value
    Assert-S1Identity $jsonPath 2570 $script:OriginSha
    $tsvPath=Resolve-S1Path $Root $value.manifest.path
    if (![IO.File]::Exists($tsvPath)) { Fail-S1 'FileMissing' }
    $rows=ConvertFrom-S1OriginManifest ([IO.File]::ReadAllBytes($tsvPath))
    return [pscustomobject]@{contract=$value;rows=$rows;originSha256=$script:OriginSha;manifestSha256=$script:ManifestSha}
}
function Get-S1BlobOid($Bytes) {
    if ($Bytes -isnot [byte[]]) { Fail-S1 'BytesType' }
    $prefix=[Text.Encoding]::ASCII.GetBytes('blob '+$Bytes.Length+[char]0)
    $combined=[byte[]]::new($prefix.Length+$Bytes.Length)
    [Array]::Copy($prefix,$combined,$prefix.Length)
    [Array]::Copy($Bytes,0,$combined,$prefix.Length,$Bytes.Length)
    return [Convert]::ToHexString([Security.Cryptography.SHA1]::HashData($combined)).ToLowerInvariant()
}
function Assert-S1TreeRecord($Row, $Record) {
    Assert-S1String $Row.mode
    Assert-S1String $Row.gitBlob
    Assert-S1RelativePath $Row.path
    if ($Row.mode -cnotmatch '^100(?:644|755)$' -or $Row.gitBlob -cnotmatch '^[a-f0-9]{40}$') { Fail-S1 'TreeIdentity' }
    Assert-S1Object $Record @('mode','type','oid','path')
    Assert-S1ExactString $Record.type 'blob' 'TreeIdentity'
    Assert-S1ExactString $Record.mode $Row.mode 'TreeIdentity'
    Assert-S1ExactString $Record.oid $Row.gitBlob 'TreeIdentity'
    Assert-S1ExactString $Record.path $Row.path 'TreeIdentity'
}
function Invoke-S1Git($Root, $Arguments) {
    # Private fixed call sites only; no public arbitrary-command export.
    $psi=[Diagnostics.ProcessStartInfo]::new('git')
    $psi.UseShellExecute=$false
    $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true
    $psi.StandardOutputEncoding=[Text.UTF8Encoding]::new($false,$true)
    foreach ($key in @($psi.Environment.Keys | Where-Object { $_.StartsWith('GIT_',[StringComparison]::OrdinalIgnoreCase) })) { [void]$psi.Environment.Remove($key) }
    $psi.Environment['GIT_CONFIG_NOSYSTEM']='1'
    $psi.Environment['GIT_CONFIG_GLOBAL']='NUL'
    $psi.Environment['GIT_TERMINAL_PROMPT']='0'
    $psi.Environment['GIT_NO_LAZY_FETCH']='1'
    foreach ($arg in @('--no-optional-locks','--no-lazy-fetch','--no-replace-objects','-c',('safe.directory='+$Root),'-c','protocol.allow=never','-c','core.fsmonitor=false','-c','core.untrackedCache=false','-C',$Root)+$Arguments) { $psi.ArgumentList.Add($arg) }
    $process=[Diagnostics.Process]::Start($psi)
    try {
        $output=$process.StandardOutput.ReadToEndAsync(); $errorText=$process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(15000)) { $process.Kill($true); Fail-S1 'GitTimeout' }
        $text=$output.GetAwaiter().GetResult(); [void]$errorText.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { Fail-S1 'GitRead' }
        return $text.TrimEnd([char]13,[char]10)
    } finally { $process.Dispose() }
}
function Assert-S1CheckoutLayout($Root) {
    $rootPath=Get-S1Absolute $Root
    Assert-S1NoReparse $rootPath
    $git=Resolve-S1Path $rootPath '.git'
    if (![IO.Directory]::Exists($git)) { Fail-S1 'IndependentGitDirectory' }
    foreach ($relative in @('commondir','objects/info/alternates','objects/info/http-alternates')) {
        $candidate=Resolve-S1Path $git $relative
        if ([IO.File]::Exists($candidate) -or [IO.Directory]::Exists($candidate)) { Fail-S1 'SharedGit' }
    }
    # Reject reparse entries anywhere in local Git storage, even before Git reads.
    $queue=[Collections.Generic.Queue[string]]::new(); $queue.Enqueue($git)
    while ($queue.Count) {
        foreach ($entry in [IO.DirectoryInfo]::new($queue.Dequeue()).EnumerateFileSystemInfos()) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Fail-S1 'ReparsePoint' }
            if (($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0) { $queue.Enqueue($entry.FullName) }
        }
    }
    return $rootPath
}
function Assert-S1Checkout($Root, $Revision) {
    Assert-S1String $Revision
    if ($Revision -cnotmatch '^[a-f0-9]{40}$') { Fail-S1 'GitRevision' }
    $rootPath=Assert-S1CheckoutLayout $Root
    $git=Join-Path $rootPath '.git'
    $common=Invoke-S1Git $rootPath @('rev-parse','--path-format=absolute','--git-common-dir')
    if (![string]::Equals((Get-S1Absolute $common),$git,[StringComparison]::OrdinalIgnoreCase)) { Fail-S1 'SharedGit' }
    $top=Invoke-S1Git $rootPath @('rev-parse','--show-toplevel')
    if (![string]::Equals((Get-S1Absolute $top),$rootPath,[StringComparison]::OrdinalIgnoreCase)) { Fail-S1 'GitRoot' }
    if ((Invoke-S1Git $rootPath @('rev-parse','--show-object-format')) -cne 'sha1') { Fail-S1 'GitObjectFormat' }
    if ((Invoke-S1Git $rootPath @('rev-parse','HEAD')) -cne $Revision) { Fail-S1 'CheckoutRevision' }
    if ((Invoke-S1Git $rootPath @('status','--porcelain=v1','--untracked-files=all'))) { Fail-S1 'DirtyCheckout' }
}
function Get-S1Files($Root, $SkipGitDirectories = @()) {
    $rootPath=Get-S1Absolute $Root
    Assert-S1NoReparse $rootPath
    if (![IO.Directory]::Exists($rootPath)) { Fail-S1 'RootMissing' }
    if ($SkipGitDirectories -isnot [array]) { Fail-S1 'ArrayShape' }
    $files=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue=[Collections.Generic.Queue[string]]::new(); $queue.Enqueue($rootPath)
    while ($queue.Count) {
        foreach ($entry in [IO.DirectoryInfo]::new($queue.Dequeue()).EnumerateFileSystemInfos()) {
            $relative=[IO.Path]::GetRelativePath($rootPath,$entry.FullName).Replace('\','/')
            Assert-S1RelativePath $relative
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Fail-S1 'ReparsePoint' }
            if (!$seen.Add($relative)) { Fail-S1 'PathCollision' }
            if ($relative -cin $SkipGitDirectories) {
                if (($entry.Attributes -band [IO.FileAttributes]::Directory) -eq 0) { Fail-S1 'IndependentGitDirectory' }
                continue
            }
            if (($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0) { $queue.Enqueue($entry.FullName) }
            else { $files.Add($relative,$entry.FullName) }
        }
    }
    return ,$files
}
function Assert-S1InputSet($Expected, $Actual) {
    if ($Expected -isnot [array] -or $Actual -isnot [array]) { Fail-S1 'ArrayShape' }
    $set=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $exact=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in $Expected) { Assert-S1RelativePath $path; if (!$set.Add($path)) { Fail-S1 'PathCollision' }; [void]$exact.Add($path) }
    if ($Expected.Count -ne $Actual.Count) { Fail-S1 'InputSet' }
    $actualSet=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $Actual) {
        Assert-S1RelativePath $path
        if (!$actualSet.Add($path)) { Fail-S1 'PathCollision' }
        if (!$exact.Contains($path)) { Fail-S1 'InputSet' }
    }
}
function Test-S1OriginalSource($ContractRoot, $OriginalRoot) {
    $origin=Read-S1OriginContract $ContractRoot
    $rootPath=Get-S1Absolute $OriginalRoot
    $records=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    $links=[Collections.Generic.List[object]]::new()
    for ($i=0; $i -lt 4; $i++) {
        $prefix=$origin.contract.checkouts[$i].path
        $checkout=if ($i -eq 0) { $rootPath } else { Resolve-S1Path $rootPath $prefix }
        Assert-S1Checkout $checkout $origin.contract.checkouts[$i].revision
        $tree=Invoke-S1Git $checkout @('ls-tree','-r','-z','HEAD')
        foreach ($line in $tree.Split([char]0)) {
            if (!$line) { continue }
            if ($line -cnotmatch '^([0-9]{6}) (blob|commit) ([a-f0-9]{40})\x09([^\r\n]+)$') { Fail-S1 'GitTreeSyntax' }
            $entry=@{mode=$Matches[1];type=$Matches[2];oid=$Matches[3];path=$Matches[4]}
            Assert-S1RelativePath $entry.path
            if ($entry.type -ceq 'commit') {
                if ($i -ne 0) { Fail-S1 'NestedGitlink' }
                $links.Add($entry); continue
            }
            $key=if ($i -eq 0) { $entry.path } else { $prefix+'/'+$entry.path }
            $entry.path=$key
            if ($records.ContainsKey($key)) { Fail-S1 'PathCollision' }
            $records.Add($key,$entry)
        }
    }
    Assert-S1Gitlinks $links.ToArray()
    $files=Get-S1Files $rootPath @('.git','third_party/highway/.git','third_party/skcms/.git','third_party/libjpeg-turbo/.git')
    $paths=@($origin.rows | ForEach-Object path)
    Assert-S1InputSet $paths @($records.Keys)
    Assert-S1InputSet $paths @($files.Keys)
    foreach ($row in $origin.rows) {
        Assert-S1TreeRecord $row $records[$row.path]
        $bytes=[IO.File]::ReadAllBytes($files[$row.path])
        if ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)) -cne $row.originalSha256 -or (Get-S1BlobOid $bytes) -cne $row.gitBlob) { Fail-S1 'OriginalBytes' }
    }
    return [pscustomobject]@{result='PASS';rows=1481;checkouts=4;gitlinks=9;derivedRegenerated=$false}
}
Export-ModuleMember -Function Read-S1OriginContract,Read-S1StrictJson,ConvertFrom-S1StrictJson,Assert-S1OriginSchema,ConvertFrom-S1OriginManifest,Assert-S1Identity,Assert-S1String,Assert-S1RelativePath,Resolve-S1Path,Assert-S1NoReparse,Assert-S1InputSet,Get-S1Files,Get-S1BlobOid,Assert-S1TreeRecord,Assert-S1CheckoutLayout,Assert-S1Checkout,Test-S1OriginalSource
