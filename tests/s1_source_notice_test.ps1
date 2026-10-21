#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# NOTICE and whole publication path tests share this planned file; no extra scope file.
param([Parameter(Mandatory)][string]$Root,[Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot ../packaging/S1SourceNotice.psm1) -Force
Import-Module (Join-Path $PSScriptRoot ../tools/S1SourceContracts.psm1)
$utf8=[Text.UTF8Encoding]::new($false)
$run=Join-Path $EvidenceRoot ('fixtures-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff'))
if(Test-Path -LiteralPath $run){throw 'Fixture collision'}
[void][IO.Directory]::CreateDirectory($run)
$results=[Collections.Generic.List[object]]::new();$notTested=[Collections.Generic.List[string]]::new()
$catalog=Read-S1NoticeCatalog $Root
$anchorBytes=[IO.File]::ReadAllBytes((Join-Path $Root $catalog.anchor.path))
$inputBytes=[IO.File]::ReadAllBytes((Join-Path $Root packaging/public/source-notices-s1-source-release-v2.json))
function Case($Id,$Kind,$Expected,$Body){
 $actual='ACCEPT';$detail=$null
 try{& $Body|Out-Null}catch{$actual=$_.Exception.Message;$detail=$_.ScriptStackTrace}
 $pass=if($Kind -ceq 'positive'){$actual -ceq 'ACCEPT'}else{$actual -ceq ('S1:'+$Expected)}
 $results.Add([pscustomobject]@{id=$Id;kind=$Kind;expected=$Expected;actual=$actual;result=$(if($pass){'PASS'}else{'FAIL'});failureDetail=$(if($pass){$null}else{$detail})})
}
function Clone-Anchor {return ,(ConvertFrom-S1StrictJson $anchorBytes)}
function Clone-Catalog {return ,(Read-S1StrictJson (Join-Path $Root packaging/public/profiles-s1-source-release-v2.json))}
function Copy-View($Name){
 $to=Join-Path $run $Name
 foreach($relative in $catalog.publicationPaths){$dest=Join-Path $to $relative;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest));[IO.File]::Copy((Join-Path $Root $relative),$dest,$false)}
 return $to
}
function Edit-Anchor($Id,$Expected,$Mutation){Case $Id negative $Expected {$a=Clone-Anchor;& $Mutation $a;Assert-S1NoticeAnchor $a $catalog}}
Case 'candidate.normal-evidence' positive ACCEPT {Read-S1SourceState $Root}
Case 'modules.helper-exports-survive-reader-reload' positive ACCEPT {
 Import-Module (Join-Path $PSScriptRoot ../packaging/S1SourceNotice.psm1) -Force
 if(!(Get-Command ConvertFrom-S1StrictJson -ErrorAction SilentlyContinue)){throw 'Helper export removed'}
 Read-S1SourceState $Root
}
Case 'candidate.clean-full-scope' positive ACCEPT {Read-S1SourceState $Root -PublicationView}
Case 'catalog.exact-policy' positive ACCEPT {Assert-S1NoticeCatalog $catalog}
Case 'anchor.exact-policy' positive ACCEPT {Assert-S1NoticeAnchor (Clone-Anchor) $catalog}
Case 'input.exact-candidate-request' positive ACCEPT {Assert-S1NoticeInput (ConvertFrom-S1StrictJson $inputBytes)}
Case 'state.declaration-is-not-owner-evidence' positive ACCEPT {$r=Read-S1SourceState $Root;if(!$r.declaredSourceReleaseApproved -or $r.binaryReleaseApproved -or $r.binaryProfiles -ne 0 -or $r.wholeScopeBytesAuthenticated -or $r.ownerApprovalEvidenceVerified -or $r.publicationPermissionVerified -or $r.candidateOnly){throw 'Approval claim'}}
Case 'candidate.no-legacy-dependency' positive ACCEPT {foreach($p in @('packaging/PortablePackage.psm1','packaging/ShippingSourceNotice.psm1','cmake/privacy/managed-sources-v1.tsv','vcpkg-overlays/libjxl/portfile.cmake')){if(Test-Path (Join-Path $Root $p)){throw 'Legacy source present'}};Read-S1SourceState $Root}
Case 'candidate.58-evidence-and-23-components' positive ACCEPT {$r=Read-S1SourceState $Root;if($r.direct -ne 15 -or $r.raw -ne 43 -or $r.components -ne 23 -or $r.licenses -ne 22 -or $r.sourceInputs -ne 28){throw 'Counts'}}
foreach($variant in @('array','null','number','bool','object','empty','space')){
 Edit-Anchor "types.anchorId.$variant" ScalarString {
  param($a)
  $a.anchorId=switch($variant){array{,@('clipss2-source-s1-release-notices-v2')};null{$null};number{1};bool{$false};object{@{x=1}};empty{''};space{' '}}
 }
}
foreach($field in @('path','sha256','gitBlob','mode','profile','upstreamVersion','attributionDocument','distributionRole')){
 Edit-Anchor "types.direct.$field.array" ScalarString {param($a)$a.fileAttributions[0][$field]=@($a.fileAttributions[0][$field])}
}
foreach($field in @('name','version','licenseExpression','selectedLicense','selectionStatus','distributionScope')){
 Edit-Anchor "types.component.$field.array" ScalarString {param($a)$a.components[0][$field]=@($a.components[0][$field])}
}
Edit-Anchor types.raw-path.array ScalarString {param($a)$a.rawFiles[0].path=@($a.rawFiles[0].path)}
Edit-Anchor types.raw-size.string IdentityType {param($a)$a.rawFiles[0].size=[string]$a.rawFiles[0].size}
Edit-Anchor types.schema-string IntegerValue {param($a)$a.schemaVersion='2'}
Edit-Anchor schema.unknown Fields {param($a)$a.extra=1}
Edit-Anchor schema.missing Fields {param($a)$a.Remove('anchorId')}
Edit-Anchor schema.nested-unknown Fields {param($a)$a.components[0].extra=1}
Edit-Anchor schema.direct-missing ArrayShape {param($a)$a.fileAttributions=$a.fileAttributions[0..13]}
Edit-Anchor schema.raw-missing ArrayShape {param($a)$a.rawFiles=$a.rawFiles[0..41]}
Edit-Anchor schema.direct-order DirectPolicy {param($a)$t=$a.fileAttributions[0];$a.fileAttributions[0]=$a.fileAttributions[1];$a.fileAttributions[1]=$t}
Edit-Anchor schema.raw-order RawId {param($a)$t=$a.rawFiles[0];$a.rawFiles[0]=$a.rawFiles[1];$a.rawFiles[1]=$t}
Edit-Anchor schema.component-element ObjectType {param($a)$a.components[0]='text'}
Edit-Anchor schema.notice-set-order NoticeSets {param($a)$t=$a.noticeSets[0];$a.noticeSets[0]=$a.noticeSets[1];$a.noticeSets[1]=$t}
Edit-Anchor rights.source-inconsistent Approval {param($a)$a.approval.sourceReleaseApproved=$false}
Edit-Anchor rights.binary-approved Approval {param($a)$a.approval.binaryReleaseApproved=$true}
Edit-Anchor rights.approval-array Approval {param($a)$a.approval.sourceReleaseApproved=@($false)}
Edit-Anchor rights.open-right-changed Approval {param($a)$a.approval.resolvedRights[0]='closed'}
Edit-Anchor rights.resolved-right-missing ArrayShape {param($a)$a.approval.resolvedRights=@()}
Edit-Anchor rights.component-promotion ComponentPolicy {param($a)$a.components[0].distributionScope='source-direct'}
Edit-Anchor rights.unknown-component ComponentPolicy {param($a)$a.components[0].id='unregistered'}
Edit-Anchor rights.cc0-missing ArrayShape {param($a)$c=@($a.components|Where-Object id -CEQ highway-source)[0];$c.exceptions=@()}
Edit-Anchor rights.license-promoted ComponentPolicy {param($a)$c=@($a.components|Where-Object id -CEQ skcms-reference)[0];$c.selectedLicense='MIT'}
Edit-Anchor rights.copyright-changed DirectPolicy {param($a)$a.fileAttributions[0].originalCopyright[0]='Copyright changed'}
Edit-Anchor rights.j5a-uri-comment-reintroduced DirectPolicy {param($a)$a.fileAttributions[0].upstreamFiles[1].uri+=' (2 tokens)'}
Edit-Anchor rights.upstream-revision-changed DirectPolicy {param($a)$a.fileAttributions[0].upstreamFiles[1].revision='unreviewed'}
Edit-Anchor rights.modified-notice-removed DirectPolicy {param($a)$a.fileAttributions[0].modification='none'}
Edit-Anchor binding.rolled-back-hash SourceInputBinding {param($a)$a.sourceInputBinding.sha256='0'*64}
Case catalog.binary-profile negative ArrayShape {$c=Clone-Catalog;$c.binaryProfiles=@('binary');Assert-S1NoticeCatalog $c}
Case catalog.source-inconsistent negative Approval {$c=Clone-Catalog;$c.sourceReleaseApproved=$false;Assert-S1NoticeCatalog $c}
Case catalog.scalar-array negative ScalarString {$c=Clone-Catalog;$c.catalogId=@($c.catalogId);Assert-S1NoticeCatalog $c}
Case catalog.legacy-direct-entry negative ScopeForbidden {$c=Clone-Catalog;$c.publicationPaths[0]='vcpkg-overlays/libjxl/portfile.cmake';Assert-S1NoticeCatalog $c}
Case catalog.path-case-collision negative PathCollision {$c=Clone-Catalog;$c.publicationPaths[1]=$c.publicationPaths[0].ToUpperInvariant();Assert-S1NoticeCatalog $c}
foreach($path in @('../outside','/absolute','C:drive','//server/share','src/file:stream','src/NUL.txt','src/trailing.')){
 Case "path.$path" negative RelativePath {$c=Clone-Catalog;$c.publicationPaths[0]=$path;Assert-S1NoticeCatalog $c}
}
Case path.case-alias negative PathCaseAlias {Resolve-S1Path $Root 'readme.md'}
Case json.duplicate-key negative DuplicateJsonKey {ConvertFrom-S1StrictJson ($utf8.GetBytes('{"a":1,"a":2}'))}
Case json.nested-duplicate-key negative DuplicateJsonKey {ConvertFrom-S1StrictJson ($utf8.GetBytes('{"a":[{"b":1,"b":2}]}'))}
Case json.invalid-utf8 negative Utf8Encoding {ConvertFrom-S1StrictJson ([byte[]]@(255))}
foreach($mutation in @('catalog','catalog-same-size','anchor','anchor-same-size','direct','raw-doc','patents','source-inputs','old-catalog','old-anchor','auto-repin')){
 $expected=@{catalog='CatalogHash';'catalog-same-size'='CatalogHash';anchor='AnchorHash';'anchor-same-size'='AnchorHash';direct='RawIdentity';'raw-doc'='RawIdentity';patents='FileMissing';'source-inputs'='RawIdentity';'old-catalog'='CatalogHash';'old-anchor'='AnchorHash';'auto-repin'='CatalogHash'}[$mutation]
 Case "trust.$mutation" negative $expected {
  $view=Copy-View ('trust-'+$mutation)
  $cat="$view/packaging/public/profiles-s1-source-release-v2.json";$anc="$view/packaging/public/notice-anchor-s1-source-release-v2.json"
  switch($mutation){
   catalog{[IO.File]::AppendAllText($cat,' ',$utf8)}
   'catalog-same-size'{$b=[IO.File]::ReadAllBytes($cat);$b[0]=91;[IO.File]::WriteAllBytes($cat,$b)}
   anchor{[IO.File]::AppendAllText($anc,' ',$utf8)}
   'anchor-same-size'{$b=[IO.File]::ReadAllBytes($anc);$b[0]=91;[IO.File]::WriteAllBytes($anc,$b)}
   direct{[IO.File]::AppendAllText((Join-Path $view $catalog.directPaths[0]),' ',$utf8)}
   'raw-doc'{[IO.File]::AppendAllText("$view/THIRD-PARTY-NOTICES-S1.md",' ',$utf8)}
   patents{[IO.File]::Move("$view/licenses/libjxl/PATENTS",(Join-Path $run missing-patents))}
   'source-inputs'{[IO.File]::AppendAllText("$view/cmake/privacy/source-inputs-s1-v1.json",' ',$utf8)}
   'old-catalog'{[IO.File]::WriteAllText($cat,'{"catalogId":"clipss2-source-shipping-candidate-catalog-v2"}',$utf8)}
   'old-anchor'{[IO.File]::WriteAllText($anc,'{"anchorId":"clipss2-source-shipping-notices-v2"}',$utf8)}
   'auto-repin'{[IO.File]::AppendAllText($anc,' ',$utf8);$c=Read-S1StrictJson $cat;$c.anchor.sha256=(Get-FileHash $anc).Hash;[IO.File]::WriteAllText($cat,($c|ConvertTo-Json -Depth 30),$utf8)}
  }
  Read-S1SourceState $view -PublicationView
 }
}
foreach($extra in @('vcpkg-overlays/libjxl/private.patch','vcpkg-overlays/shipping-privacy/highway/portfile.cmake','packaging/ShippingSourceNotice.psm1','packaging/PortablePackage.psm1','unregistered.md','.git/config','build/receipt.json','cache/log.txt')){
 Case "scope.extra.$extra" negative ScopeFiles {$view=Copy-View ('extra-'+$results.Count);$path=Join-Path $view $extra;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path));[IO.File]::WriteAllText($path,'fixture',$utf8);Read-S1SourceState $view -PublicationView}
}
Case scope.unregistered-empty-directory negative ScopeDirectories {$view=Copy-View empty-dir;[void][IO.Directory]::CreateDirectory("$view/unregistered-empty");Read-S1SourceState $view -PublicationView}
Case scope.empty-git-directory negative ScopeForbidden {$view=Copy-View empty-git;[void][IO.Directory]::CreateDirectory("$view/.git");Read-S1SourceState $view -PublicationView}
Case scope.extra-overlay-empty-directory negative ScopeDirectories {$view=Copy-View empty-overlay;[void][IO.Directory]::CreateDirectory("$view/vcpkg-overlays/shipping-privacy-v2/unregistered");Read-S1SourceState $view}
Case scope.extra-license-file negative ScopeFiles {$view=Copy-View extra-license;[IO.File]::WriteAllText("$view/licenses/unregistered.txt",'fixture',$utf8);Read-S1SourceState $view}
Case scope.case-only-physical-rename negative ScopeFiles {$view=Copy-View case-rename;[IO.File]::Move("$view/README.md","$view/readme.MD");Read-S1SourceState $view -PublicationView}
Case scope.junction-reparse negative ReparsePoint {$view=Copy-View junction-view;$target=Join-Path $run junction-target;[void][IO.Directory]::CreateDirectory($target);[void](New-Item -ItemType Junction -Path "$view/reparse" -Target $target -ErrorAction Stop);Read-S1SourceState $view -PublicationView}
try{
 $view=Copy-View symlink-view
 [void](New-Item -ItemType SymbolicLink -Path "$view/symlink.txt" -Target "$view/README.md" -ErrorAction Stop)
 Case scope.file-symlink negative ReparsePoint {Read-S1SourceState $view -PublicationView}
}catch{$notTested.Add('Physical file symlink creation requires Windows privilege; not counted as PASS.')}
Case scope.path-closure-is-not-whole-byte-authentication positive ACCEPT {
 $view=Copy-View unbound-source-edit
 # Deliberately outside the 58 registered evidence files. Record design boundary.
 [IO.File]::AppendAllText("$view/README.md",'extra explanatory text',$utf8)
 $r=Read-S1SourceState $view -PublicationView
 if($r.wholeScopeBytesAuthenticated){throw 'False whole-byte authentication claim'}
}
function Development-Catalog {
 $c=Clone-Catalog;$c.status='source-development';$c.sourceReleaseApproved=$false;$c.openRights=@('s1-included-source-rights-review','s1-final-source-identity','s1-publication-owner-approval');$c.resolvedRights=@();$c.approvalReferenceId=$null
 foreach($s in $c.noticeSetPolicy){$s.status=if($s.purpose -ceq 'reference'){'reference-only'}else{$c.status}}
 return ,$c
}
Case state.development-tuple positive ACCEPT {Assert-S1NoticeCatalog (Development-Catalog)}
Case state.formal-api positive ACCEPT {$r=Read-S1SourceRelease $Root -PublicationView;if(!$r.declaredSourceReleaseApproved -or $r.ownerApprovalEvidenceVerified){throw 'Trust boundary'}}
Case state.development-anchor positive ACCEPT {$c=Development-Catalog;$a=Clone-Anchor;$a.approval=[ordered]@{status=$c.status;sourceReleaseApproved=$false;binaryReleaseApproved=$false;openRights=$c.openRights;resolvedRights=@();approvalReferenceId=$null};$a.noticeSets=$c.noticeSetPolicy;Assert-S1NoticeAnchor $a $c}
Case api.old-candidate negative CandidateContractUnsupported {Read-S1NoticeCandidate $Root}
Case state.unknown negative SourceState {$c=Clone-Catalog;$c.status='approved';Assert-S1NoticeCatalog $c}
Case state.binary-true negative BinaryApprovalForbidden {$c=Clone-Catalog;$c.binaryReleaseApproved=$true;Assert-S1NoticeCatalog $c}
Case state.binary-string negative BinaryApprovalForbidden {$c=Clone-Catalog;$c.binaryReleaseApproved='false';Assert-S1NoticeCatalog $c}
Case state.source-string negative Approval {$c=Clone-Catalog;$c.sourceReleaseApproved='true';Assert-S1NoticeCatalog $c}
Case state.source-array negative Approval {$c=Clone-Catalog;$c.sourceReleaseApproved=@($true);Assert-S1NoticeCatalog $c}
Case state.ref-null negative ApprovalReference {$c=Clone-Catalog;$c.approvalReferenceId=$null;Assert-S1NoticeCatalog $c}
Case state.ref-array negative ApprovalReference {$c=Clone-Catalog;$c.approvalReferenceId=@($c.approvalReferenceId);Assert-S1NoticeCatalog $c}
Case state.ref-malformed negative ApprovalReference {$c=Clone-Catalog;$c.approvalReferenceId='owner-approved';Assert-S1NoticeCatalog $c}
Case state.ref-missing-field negative Fields {$c=Clone-Catalog;$c.Remove('approvalReferenceId');Assert-S1NoticeCatalog $c}
Case state.rights-partial negative ArrayShape {$c=Clone-Catalog;$c.resolvedRights=$c.resolvedRights[0..1];Assert-S1NoticeCatalog $c}
Case state.rights-order negative RightsResolution {$c=Clone-Catalog;$t=$c.resolvedRights[0];$c.resolvedRights[0]=$c.resolvedRights[1];$c.resolvedRights[1]=$t;Assert-S1NoticeCatalog $c}
Case state.rights-duplicate negative RightsResolution {$c=Clone-Catalog;$c.resolvedRights[1]=$c.resolvedRights[0];Assert-S1NoticeCatalog $c}
Case state.rights-unknown negative RightsResolution {$c=Clone-Catalog;$c.resolvedRights[0]='self-approved';Assert-S1NoticeCatalog $c}
Case state.open-not-empty negative ArrayShape {$c=Clone-Catalog;$c.openRights=@('s1-included-source-rights-review');Assert-S1NoticeCatalog $c}
Case state.development-reference negative ApprovalReference {$c=Development-Catalog;$c.approvalReferenceId=$catalog.approvalReferenceId;Assert-S1NoticeCatalog $c}
Case state.development-true negative Approval {$c=Development-Catalog;$c.sourceReleaseApproved=$true;Assert-S1NoticeCatalog $c}
Case state.old-schema negative IntegerValue {$c=Clone-Catalog;$c.schemaVersion=1;Assert-S1NoticeCatalog $c}
Case state.old-purpose negative NoticeInput {$i=ConvertFrom-S1StrictJson $inputBytes;$i.purpose='source-notice-candidate';Assert-S1NoticeInput $i}
Case scope.old-catalog-path negative ScopeForbidden {$c=Clone-Catalog;$c.publicationPaths[0]='packaging/public/profiles-s1-candidate-v1.json';Assert-S1NoticeCatalog $c}
Case scope.old-draft-path negative ScopeForbidden {$c=Clone-Catalog;$c.publicationPaths[0]='THIRD-PARTY-NOTICES-S1-DRAFT.md';Assert-S1NoticeCatalog $c}
Case state.reference-not-approved negative NoticeSets {$c=Clone-Catalog;$c.noticeSetPolicy[2].status='source-release-approved';Assert-S1NoticeCatalog $c}

Case candidate.final-after-all-negatives positive ACCEPT {Read-S1SourceState $Root -PublicationView}
$summary=[ordered]@{result=$(if(@($results|Where-Object result -CEQ FAIL).Count){'FAIL'}else{'PASS'});positive=@($results|Where-Object kind -CEQ positive).Count;negative=@($results|Where-Object kind -CEQ negative).Count;failed=@($results|Where-Object result -CEQ FAIL).Count;cases=@($results);notTested=@($notTested);semanticTests='Canonical code-pinned catalog policy vs mutated in-memory anchor; no runtime repinning, no claim of HTTP/archive verification';scopeBoundary='Full path/dir closure plus 58 evidence hashes; all source bytes/modes independently fixed outside reader';productBuild=$false}
[IO.File]::WriteAllText("$run/results.json",($summary|ConvertTo-Json -Depth 12),$utf8)
[ordered]@{receipt="$run/results.json";result=$summary.result;positive=$summary.positive;negative=$summary.negative;failed=$summary.failed;notTested=$summary.notTested;failures=@($results|Where-Object result -CEQ FAIL)}|ConvertTo-Json -Depth 8
if($summary.failed){exit 1}
