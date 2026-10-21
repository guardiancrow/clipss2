#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$work=(Resolve-Path "$PSScriptRoot/..").Path
Import-Module "$work/tools/S1SourceContracts.psm1" -Force
$module=Get-Module S1SourceContracts
$run=Join-Path $EvidenceRoot ('fixtures-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff'))
if(Test-Path -LiteralPath $run){throw 'Fixture collision'}
[void][IO.Directory]::CreateDirectory($run)
$results=[Collections.Generic.List[object]]::new()
$utf8=[Text.UTF8Encoding]::new($false)
$originalJson=[IO.File]::ReadAllBytes("$work/cmake/privacy/managed-origin-s1-v1.json")
$originalTsv=[IO.File]::ReadAllBytes("$work/cmake/privacy/managed-origin-s1-v1.tsv")
$canonical=ConvertFrom-S1StrictJson $originalJson
function Clone-Contract { return ,(ConvertFrom-S1StrictJson $originalJson) }
function Case($Id,$Kind,$Expected,$Body) {
    $actual=$null; $detail=$null
    try { & $Body | Out-Null; $actual='ACCEPT' }
    catch { $actual=$_.Exception.Message; $detail=$_.ScriptStackTrace }
    $passed=if($Kind -ceq 'positive'){$actual -ceq 'ACCEPT'}else{$actual -ceq ('S1:'+$Expected)}
    $results.Add([pscustomobject]@{id=$Id;kind=$Kind;expected=$Expected;actual=$actual;result=$(if($passed){'PASS'}else{'FAIL'});failureDetail=$(if($passed){$null}else{$detail})})
}
function Change-Field($Value,$Path,$Replacement) {
    $cursor=$Value
    for($i=0;$i -lt $Path.Count-1;$i++){$cursor=$cursor[$Path[$i]]}
    $cursor[$Path[-1]]=$Replacement
}
function Save-Utf8($Path,$Text) { [IO.File]::WriteAllText($Path,$Text,$utf8) }
function Tsv-Lines { return ,($utf8.GetString($originalTsv).TrimEnd([char]10).Split([char]10)) }
function Manifest-FromLines($Lines) { ConvertFrom-S1OriginManifest ($utf8.GetBytes(($Lines -join "`n")+"`n")) }

Case 'origin.accept' positive ACCEPT { $loaded=Read-S1OriginContract $work; if($loaded.rows.Count -ne 1481){throw 'Row assertion'} }
Case 'origin.schema' positive ACCEPT { Assert-S1OriginSchema $canonical }
Case 'origin.tsv' positive ACCEPT { $rows=ConvertFrom-S1OriginManifest $originalTsv; if($rows.Count -ne 1481){throw 'Rows'} }
Case 'origin.4-checkouts' positive ACCEPT { if($canonical.checkouts.Count -ne 4){throw 'Checkouts'}; Assert-S1OriginSchema $canonical }
Case 'origin.9-gitlinks' positive ACCEPT { if($canonical.gitlinks.Count -ne 9){throw 'Gitlinks'}; Assert-S1OriginSchema $canonical }
Case 'origin.raw-identities' positive ACCEPT {
    Assert-S1Identity "$work/cmake/privacy/managed-origin-s1-v1.json" 2570 '2E96789787E73F37A8278842ED5258CE71976D08935D644EFDDA8FBA8F07D563'
    Assert-S1Identity "$work/cmake/privacy/managed-origin-s1-v1.tsv" 231616 '7D4BC1700562893F34721C9AECE02C5D42672C21EB9071341BD17107DD53777D'
}
$isolated=Join-Path $run isolated
[void][IO.Directory]::CreateDirectory("$isolated/cmake/privacy")
[IO.File]::WriteAllBytes("$isolated/cmake/privacy/managed-origin-s1-v1.json",$originalJson)
[IO.File]::WriteAllBytes("$isolated/cmake/privacy/managed-origin-s1-v1.tsv",$originalTsv)
Case 'origin.no-legacy-contract-present' positive ACCEPT {
    $files=Get-S1Files $isolated
    Assert-S1InputSet @('cmake/privacy/managed-origin-s1-v1.json','cmake/privacy/managed-origin-s1-v1.tsv') @($files.Keys)
    Read-S1OriginContract $isolated
}
Case 'json.nested-valid-types' positive ACCEPT { ConvertFrom-S1StrictJson ($utf8.GetBytes('{"a":["s",null,1,true,{}],"b":false}')) }
Case 'json.duplicate' negative DuplicateJsonKey { ConvertFrom-S1StrictJson ($utf8.GetBytes('{"a":1,"a":2}')) }
Case 'json.escaped-duplicate' negative DuplicateJsonKey { ConvertFrom-S1StrictJson ($utf8.GetBytes('{"a":1,"\u0061":2}')) }
Case 'json.case-alias-key' negative DuplicateJsonKey { ConvertFrom-S1StrictJson ($utf8.GetBytes('{"A":1,"a":2}')) }
Case 'json.nested-duplicate' negative DuplicateJsonKey { ConvertFrom-S1StrictJson ($utf8.GetBytes('{"a":[{"b":1,"b":2}]}')) }
foreach($text in @('{"a":1,}','{/* comment */"a":1}','{"a":NaN}','{"a":1}{}')) {
    Case "json.syntax.$text" negative JsonSyntax { ConvertFrom-S1StrictJson ($utf8.GetBytes($text)) }
}
foreach($text in @('[]','null','true','1','"text"')) { Case "json.root.$text" negative JsonRootObject { ConvertFrom-S1StrictJson ($utf8.GetBytes($text)) } }
Case 'json.bom' negative Utf8Bom { ConvertFrom-S1StrictJson ([byte[]](@(239,187,191)+$originalJson)) }
Case 'json.invalid-utf8' negative Utf8Encoding { ConvertFrom-S1StrictJson ([byte[]]@(123,34,97,34,58,34,255,34,125)) }
Case 'json.non-bytes' negative BytesType { ConvertFrom-S1StrictJson @('1') }

# Every scalar string field is exercised with all problematic PowerShell shapes.
$scalarPaths=[Collections.Generic.List[object]]::new()
$scalarPaths.Add(@('contractId'));$scalarPaths.Add(@('sourceScopeId'))
for($i=0;$i -lt 4;$i++){foreach($field in @('path','uri','revision')){$scalarPaths.Add(@('checkouts',$i,$field))}}
for($i=0;$i -lt 9;$i++){foreach($field in @('path','mode','type','oid')){$scalarPaths.Add(@('gitlinks',$i,$field))}}
$scalarPaths.Add(@('manifest','path'));$scalarPaths.Add(@('manifest','sha256'))
for($i=0;$i -lt 4;$i++){$scalarPaths.Add(@('manifest','columns',$i))}
foreach($path in $scalarPaths){
    foreach($variant in @('array','object','null','number','bool','empty','space')){
        Case ('schema.scalar.'+($path -join '.')+'.'+$variant) negative ScalarString {
            $value=Clone-Contract
            $replacement=switch($variant){'array'{,@('x')};'object'{@{x='x'}};'null'{$null};'number'{1};'bool'{$true};'empty'{''};'space'{'   '}}
            Change-Field $value $path $replacement
            Assert-S1OriginSchema $value
        }
    }
}
foreach($path in @(@('schemaVersion'),@('manifest','size'),@('manifest','rowCount'))){
    foreach($variant in @('string','array','object','bool','null','float')){
        Case ('schema.integer.'+($path -join '.')+'.'+$variant) negative IntegerValue {
            $value=Clone-Contract
            $replacement=switch($variant){'string'{'1'};'array'{,@(1)};'object'{@{x=1}};'bool'{$true};'null'{$null};'float'{[double]1}}
            Change-Field $value $path $replacement
            Assert-S1OriginSchema $value
        }
    }
}
foreach($variant in @('missing','unknown','case')){
    Case "schema.fields.$variant" negative Fields {
        $value=Clone-Contract
        switch($variant){'missing'{$value.Remove('contractId')};'unknown'{$value.extra=1};'case'{$value.ContractID=$value.contractId;$value.Remove('contractId')}}
        Assert-S1OriginSchema $value
    }
}
Case 'schema.nested-unknown' negative Fields { $v=Clone-Contract;$v.checkouts[0].extra=1;Assert-S1OriginSchema $v }
Case 'schema.null-object' negative ObjectType { $v=Clone-Contract;$v.manifest=$null;Assert-S1OriginSchema $v }
Case 'schema.array-root' negative ObjectType { Assert-S1OriginSchema @($canonical) }
Case 'schema.checkouts-count' negative ArrayShape { $v=Clone-Contract;$v.checkouts=$v.checkouts[0..2];Assert-S1OriginSchema $v }
Case 'schema.checkouts-order' negative Checkout { $v=Clone-Contract;$v.checkouts=@($v.checkouts[1],$v.checkouts[0],$v.checkouts[2],$v.checkouts[3]);Assert-S1OriginSchema $v }
Case 'schema.checkout-pin' negative Checkout { $v=Clone-Contract;$v.checkouts[0].revision='0000000000000000000000000000000000000000';Assert-S1OriginSchema $v }
Case 'schema.checkout-uri' negative Checkout { $v=Clone-Contract;$v.checkouts[0].uri='https://example.invalid/not-approved';Assert-S1OriginSchema $v }
Case 'schema.gitlinks-missing' negative ArrayShape { $v=Clone-Contract;$v.gitlinks=$v.gitlinks[0..7];Assert-S1OriginSchema $v }
Case 'schema.gitlinks-extra' negative ArrayShape { $v=Clone-Contract;$v.gitlinks+=@($v.gitlinks[0]);Assert-S1OriginSchema $v }
Case 'schema.gitlinks-order' negative Gitlinks { $v=Clone-Contract;$temp=$v.gitlinks[0];$v.gitlinks[0]=$v.gitlinks[1];$v.gitlinks[1]=$temp;Assert-S1OriginSchema $v }
Case 'schema.gitlinks-oid' negative Gitlinks { $v=Clone-Contract;$v.gitlinks[0].oid='0000000000000000000000000000000000000000';Assert-S1OriginSchema $v }
Case 'schema.v1-contract' negative StringValue { $v=Clone-Contract;$v.contractId='clipss2-managed-diagnostics-v1';Assert-S1OriginSchema $v }
Case 'schema.scope-unknown' negative Scope { $v=Clone-Contract;$v.sourceScopeId='unknown';Assert-S1OriginSchema $v }
Case 'schema.profile-v1-not-a-field' negative Fields { $v=Clone-Contract;$v.profile='v1';Assert-S1OriginSchema $v }
Case 'schema.privacy-off-not-a-field' negative Fields { $v=Clone-Contract;$v.privacy=$false;Assert-S1OriginSchema $v }
Case 'schema.unregistered-manifest' negative ManifestPath { $v=Clone-Contract;$v.manifest.path='cmake/privacy/unregistered.tsv';Assert-S1OriginSchema $v }
Case 'schema.tsv-expected-hash' negative ManifestHash { $v=Clone-Contract;$v.manifest.sha256=('0'*64);Assert-S1OriginSchema $v }
Case 'schema.columns-order' negative Columns { $v=Clone-Contract;$v.manifest.columns=@('mode','originalSha256','gitBlob','path');Assert-S1OriginSchema $v }
Case 'origin.no-v1-fallback' negative FileMissing {
    $root=Join-Path $run only-old-names;[void][IO.Directory]::CreateDirectory("$root/cmake/privacy")
    Save-Utf8 "$root/cmake/privacy/managed-diagnostics-v1.json" '{}'
    Read-S1OriginContract $root
}
Case 'origin.json-raw-format-change' negative RawIdentity {
    $root=Join-Path $run changed-json;[void][IO.Directory]::CreateDirectory("$root/cmake/privacy")
    [IO.File]::WriteAllBytes("$root/cmake/privacy/managed-origin-s1-v1.json",[byte[]]($originalJson+32))
    Read-S1OriginContract $root
}
Case 'tsv.1480' negative RowCount { $lines=Tsv-Lines;Manifest-FromLines $lines[0..1479] }
Case 'tsv.1482' negative RowCount { $lines=Tsv-Lines;Manifest-FromLines ($lines+@($lines[-1])) }
Case 'tsv.crlf' negative TsvLf { ConvertFrom-S1OriginManifest ($utf8.GetBytes($utf8.GetString($originalTsv).Replace("`n","`r`n"))) }
Case 'tsv.no-final-lf' negative TsvLf { ConvertFrom-S1OriginManifest ($utf8.GetBytes($utf8.GetString($originalTsv).TrimEnd([char]10))) }
Case 'tsv.bom' negative Utf8Bom { ConvertFrom-S1OriginManifest ([byte[]](@(239,187,191)+$originalTsv)) }
foreach($column in @(0,1,2,3)) {
    $expected=@('OriginalSha','GitMode','GitBlob','RelativePath')[$column]
    Case "tsv.malformed-column.$column" negative $expected {
        $lines=Tsv-Lines;$parts=$lines[0].Split([char]9);$parts[$column]=@('bad','160000','wrong','../escape')[$column];$lines[0]=$parts -join "`t";Manifest-FromLines $lines
    }
}
foreach($column in @(0,1,2,3)){
    Case "tsv.valid-shaped-tuple-changed.$column" negative ManifestRawIdentity {
        $lines=Tsv-Lines;$parts=$lines[0].Split([char]9)
        $parts[$column]=@(('0'*64),'100755',('0'*40),'.aaaaaaaaa')[$column]
        $lines[0]=$parts -join "`t";Manifest-FromLines $lines
    }
}
Case 'tsv.case-collision' negative PathCollision {
    $lines=Tsv-Lines;$first=$lines[0].Split([char]9);$second=$lines[1].Split([char]9);$second[3]=$first[3].ToUpperInvariant();$lines[1]=$second -join "`t";Manifest-FromLines $lines
}
Case 'tsv.path-order' negative PathOrder { $lines=Tsv-Lines;$t=$lines[0];$lines[0]=$lines[1];$lines[1]=$t;Manifest-FromLines $lines }
Case 'tsv.extra-derived-column' negative TsvColumns { $lines=Tsv-Lines;$lines[0]+="`t"+('0'*64);Manifest-FromLines $lines }

$fs=Join-Path $run filesystem
[void][IO.Directory]::CreateDirectory("$fs/Sub")
Save-Utf8 "$fs/Sub/Exact.txt" 'abc'
Case 'path.valid' positive ACCEPT { Resolve-S1Path $fs 'Sub/Exact.txt' }
foreach($relative in @('../escape','Sub/../escape','/absolute','C:relative','C:/absolute','//server/share','\\server\share','Sub\Exact.txt','Sub//Exact.txt','Sub/','./Sub','Sub/NUL.txt','Sub/CON','Sub/name.','Sub/name ','Sub/file:stream')){
    Case "path.reject.$relative" negative RelativePath { Resolve-S1Path $fs $relative }
}
Case 'path.case-alias' negative PathCaseAlias { Resolve-S1Path $fs 'sub/Exact.txt' }
Case 'path.file-case-alias' negative PathCaseAlias { Resolve-S1Path $fs 'Sub/exact.txt' }
Case 'path.typed-array' negative ScalarString { Resolve-S1Path $fs @('Sub/Exact.txt') }
Case 'files.exact' positive ACCEPT { $files=Get-S1Files $fs;Assert-S1InputSet @('Sub/Exact.txt') @($files.Keys) }
Case 'files.missing' negative InputSet { Assert-S1InputSet @('Sub/Exact.txt','missing') @('Sub/Exact.txt') }
Case 'files.extra' negative InputSet { Assert-S1InputSet @('Sub/Exact.txt') @('Sub/Exact.txt','extra') }
Case 'files.case-alias' negative InputSet { Assert-S1InputSet @('Sub/Exact.txt') @('Sub/exact.txt') }
Case 'files.case-collision' negative PathCollision { Assert-S1InputSet @('Sub/Exact.txt','sub/exact.txt') @('Sub/Exact.txt','sub/exact.txt') }
Case 'files.physical-extra' negative InputSet {
    Save-Utf8 "$fs/extra.txt" 'extra';$files=Get-S1Files $fs;Assert-S1InputSet @('Sub/Exact.txt') @($files.Keys)
}
Case 'identity.valid' positive ACCEPT { Assert-S1Identity "$fs/Sub/Exact.txt" 3 'BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD' }
Case 'identity.hash' negative RawIdentity { Assert-S1Identity "$fs/Sub/Exact.txt" 3 ('0'*64) }
Case 'identity.size' negative RawIdentity { Assert-S1Identity "$fs/Sub/Exact.txt" 4 'BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD' }
Case 'identity.string-size' negative IdentityType { Assert-S1Identity "$fs/Sub/Exact.txt" '3' ('0'*64) }
Case 'identity.array-hash' negative ScalarString { Assert-S1Identity "$fs/Sub/Exact.txt" 3 @(('0'*64)) }
Case 'git.blob-oid' positive ACCEPT { if((Get-S1BlobOid ($utf8.GetBytes('abc'))) -cne 'f2ba8f84ab5c1bce84a7b441cb1959cfc7093b7f'){throw 'Git known vector'} }
$row=[pscustomobject]@{mode='100644';gitBlob='f2ba8f84ab5c1bce84a7b441cb1959cfc7093b7f';path='source.txt'}
Case 'git.tree-record' positive ACCEPT { Assert-S1TreeRecord $row @{mode=$row.mode;oid=$row.gitBlob;type='blob';path=$row.path} }
foreach($field in @('mode','oid','type','path')){
    Case "git.tree-$field" negative TreeIdentity { $record=@{mode=$row.mode;oid=$row.gitBlob;type='blob';path=$row.path};$record[$field]='changed';Assert-S1TreeRecord $row $record }
}
$layout=Join-Path $run git-layout
[void][IO.Directory]::CreateDirectory("$layout/.git/objects/info")
Case 'git.layout-metadata-only' positive ACCEPT { Assert-S1CheckoutLayout $layout }
Case 'git.not-a-directory' negative IndependentGitDirectory {
    $root=Join-Path $run git-file;[void][IO.Directory]::CreateDirectory($root);Save-Utf8 "$root/.git" 'gitdir: external';Assert-S1CheckoutLayout $root
}
foreach($storageKind in @('alternates','http-alternates','commondir')){
    Case "git.shared.$storageKind" negative SharedGit {
        $root=Join-Path $run "git-$storageKind";[void][IO.Directory]::CreateDirectory("$root/.git/objects/info")
        $relative=if($storageKind -ceq 'commondir'){'commondir'}else{'objects/info/'+$storageKind}
        Save-Utf8 "$root/.git/$relative" 'untrusted';Assert-S1CheckoutLayout $root
    }
}
# Read-only command transcript fixtures. No repository/init/commit is created.
# These cover dirty/shared output handling, not an actual modified upstream repo.
& $module {
    $script:FixtureRoot=$args[0];$script:FixtureMode='clean'
    function script:Invoke-S1Git($Root,$Arguments) {
        switch($Arguments[0]+' '+$Arguments[1]){
            'rev-parse --git-common-dir' { throw 'Unexpected argument order' }
            'rev-parse --path-format=absolute' { if($script:FixtureMode -ceq 'shared'){return ($script:FixtureRoot+'/other')};return ($script:FixtureRoot+'/.git') }
            'rev-parse --show-toplevel' { return $script:FixtureRoot }
            'rev-parse --show-object-format' { return 'sha1' }
            'rev-parse HEAD' { return '1111111111111111111111111111111111111111' }
            'status --porcelain=v1' { if($script:FixtureMode -ceq 'dirty'){return ' M source.txt'};return '' }
            default { throw 'Unexpected read-only Git fixture command' }
        }
    }
} $layout
Case 'git.checkout-transcript-clean' positive ACCEPT { Assert-S1Checkout $layout ('1'*40) }
Case 'git.checkout-transcript-dirty' negative DirtyCheckout { & $module {$script:FixtureMode='dirty'};Assert-S1Checkout $layout ('1'*40) }
Case 'git.checkout-transcript-shared' negative SharedGit { & $module {$script:FixtureMode='shared'};Assert-S1Checkout $layout ('1'*40) }
& $module {$script:FixtureMode='clean'}
Case 'git.checkout-transcript-pin' negative CheckoutRevision { Assert-S1Checkout $layout ('2'*40) }
Remove-Module S1SourceContracts
Import-Module "$work/tools/S1SourceContracts.psm1" -Force

$notTested=[Collections.Generic.List[object]]::new()
$junction=Join-Path $run junction-link
try {
    [void](New-Item -ItemType Junction -Path $junction -Target $fs -ErrorAction Stop)
    Case 'filesystem.junction-root' negative ReparsePoint { Assert-S1NoReparse $junction }
    Case 'filesystem.junction-ancestor' negative ReparsePoint { Assert-S1NoReparse "$junction/Sub/Exact.txt" }
    Case 'filesystem.junction-enumeration' negative ReparsePoint { Get-S1Files $run }
} catch { $notTested.Add([pscustomobject]@{test='junction';reason=$_.Exception.Message}) }
$symbolic=Join-Path $run symbolic-link
try {
    [void](New-Item -ItemType SymbolicLink -Path $symbolic -Target "$fs/Sub/Exact.txt" -ErrorAction Stop)
    Case 'filesystem.symlink-file' negative ReparsePoint { Assert-S1NoReparse $symbolic }
} catch { $notTested.Add([pscustomobject]@{test='symlink-file';reason=$_.Exception.Message}) }
# No deletion of fixtures; links point only into this owned PRIVATE run.
Case 'origin.final-positive-after-negatives' positive ACCEPT { Read-S1OriginContract $work }
$summary=[ordered]@{
    result=$(if(@($results|Where-Object result -CEQ FAIL).Count){'FAIL'}else{'PASS'})
    positive=@($results|Where-Object kind -CEQ positive).Count
    negative=@($results|Where-Object kind -CEQ negative).Count
    failed=@($results|Where-Object result -CEQ FAIL).Count
    notTested=@($notTested)
    cases=@($results)
    boundary='Isolated metadata/JSON/TSV/path fixtures; Git dirty/shared outputs are explicit transcript units. No actual checkout mutation, derive, build, install, or packaging.'
    sourceInputsProductionBinding=$false
    fullProfileConsumerValidation='DEFERRED TO J6B-2'
}
[IO.File]::WriteAllText("$run/results.json",($summary|ConvertTo-Json -Depth 10),$utf8)
[ordered]@{receipt="$run/results.json";result=$summary.result;positive=$summary.positive;negative=$summary.negative;failed=$summary.failed;notTested=$summary.notTested;failures=@($results|Where-Object result -CEQ FAIL)}|ConvertTo-Json -Depth 7
if($summary.failed){exit 1}
