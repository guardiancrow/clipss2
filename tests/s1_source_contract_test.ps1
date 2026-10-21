#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
# Consumer tests only. Does not perform derive/install/build or duplicate B1 suite.
param([Parameter(Mandatory)][string]$Root,[Parameter(Mandatory)][string]$EvidenceRoot,
      [Parameter(Mandatory)][string]$OriginalRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot ../tools/ShippingProfileS1.ps1)
$utf8=[Text.UTF8Encoding]::new($false)
$run=Join-Path $EvidenceRoot ('fixtures-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff'))
if(Test-Path -LiteralPath $run){throw 'Fixture collision'}
[void][IO.Directory]::CreateDirectory($run)
$results=[Collections.Generic.List[object]]::new()
$notTested=[Collections.Generic.List[string]]::new()
function Case($Id,$Kind,$Expected,$Body){
 $actual='ACCEPT';$detail=$null
 try{& $Body|Out-Null}catch{$actual=$_.Exception.Message;$detail=$_.ScriptStackTrace}
 $pass=if($Kind -ceq 'positive'){$actual -ceq 'ACCEPT'}else{$actual -ceq 'S1:'+$Expected}
 $results.Add([pscustomobject]@{id=$Id;kind=$Kind;expected=$Expected;actual=$actual;result=$(if($pass){'PASS'}else{'FAIL'});failureDetail=$(if($pass){$null}else{$detail})})
}
function Exec($Executable,$Arguments){
 $psi=[Diagnostics.ProcessStartInfo]::new($Executable);$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 foreach($arg in $Arguments){$psi.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::Start($psi)
 try{$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit(60000)){$p.Kill($true);throw 'Fixture process timeout'};return [pscustomobject]@{code=$p.ExitCode;stdout=$out.GetAwaiter().GetResult();stderr=$err.GetAwaiter().GetResult()}}finally{$p.Dispose()}
}
function Invoke-S1FixtureCli($Script,$Arguments,$Expect=''){
 $r=Exec (Join-Path $PSHOME pwsh.exe) (@('-NoProfile','-File',$Script)+$Arguments)
 if(!$Expect){if($r.code -ne 0){throw $r.stderr};return $r.stdout|ConvertFrom-Json -AsHashtable}
 if($r.code -eq 0){throw 'Unexpected CLI acceptance'}
 if($r.stderr -notmatch ('S1:'+ [regex]::Escape($Expect)+'(?:\s|\x1b|$)')){throw ('Wrong CLI refusal: '+$r.stderr)}
 Stop-S1 $Expect
}
$selection=@('-Profile','v2','-Scope','s1-v1','-Privacy','ON','-Configuration','Release','-Architecture','x64')
$cli=Join-Path $Root tools/build_shipping_dependencies_s1.ps1
$derive=Join-Path $Root tools/derive_shipping_privacy_s1.ps1
$plan=Read-S1SourceInputs $Root
$ledger=Read-S1StrictJson (Join-Path $Root cmake/privacy/source-inputs-s1-v1.json)
function New-View($Name){
 $destination=Join-Path $run $Name
 [void][IO.Directory]::CreateDirectory($destination)
 $copyPaths=@($ledger.files.path)+@('cmake/privacy/source-inputs-s1-v1.json','tools/S1SourceContracts.psm1','tools/ShippingPrivacyS1.ps1','tools/ShippingProfileS1.ps1','tools/build_shipping_dependencies_s1.ps1','tools/derive_shipping_privacy_s1.ps1','cmake/ShippingPrivacy.cmake')
 foreach($relative in $copyPaths){$out=Join-Path $destination $relative;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($out));[IO.File]::Copy((Join-Path $Root $relative),$out,$false)}
 return $destination
}
function Clone-Ledger{return ,(ConvertFrom-S1StrictJson ([IO.File]::ReadAllBytes((Join-Path $Root cmake/privacy/source-inputs-s1-v1.json))))}
function Clone-Preset{return ,(Read-S1StrictJson (Join-Path $Root CMakePresets.json))}
function Save-Json($Path,$Value){[IO.File]::WriteAllText($Path,(($Value|ConvertTo-Json -Depth 15)+"`n"),$utf8)}
function Snapshot($Directory){
 $files=Get-S1Files $Directory
 return (@(foreach($key in ($files.Keys|Sort-Object -CaseSensitive)){ $key+' '+(Get-FileHash -LiteralPath $files[$key]).Hash }) -join "`n")
}
Case 'inputs.28-raw-identities' positive ACCEPT {$p=Read-S1SourceInputs $Root;if($p.inputs.files.Count -ne 28){throw 'Count'}}
Case 'inputs.16-packages-triplets-overlay' positive ACCEPT {if($plan.inputs.packages.Count -ne 16 -or $plan.inputs.targetTriplet -cne 'x64-windows-shipping-privacy' -or $plan.inputs.hostTriplet -cne 'x64-windows' -or $plan.inputs.overlayRoot -cne 'vcpkg-overlays/shipping-privacy-v2'){throw 'Selection'}}
Case 'managed.origin-v2-connection' positive ACCEPT {$m=Read-S1Managed $Root;if($m.rows.Count -ne 1481 -or $m.origin.contract.checkouts.Count -ne 4 -or $m.origin.contract.gitlinks.Count -ne 9 -or $m.spec.changes.Count -ne 3){throw 'Managed'}}
Case 'profile.only-supported-selection' positive ACCEPT {Assert-S1Selection v2 s1-v1 ON Release x64}
Case 'preset.exact-release' positive ACCEPT {Assert-S1Presets (Clone-Preset)}
$before=Snapshot $Root
$dirsBefore=@(Get-ChildItem -LiteralPath $Root -Directory -Recurse|ForEach-Object FullName) -join "`n"
Case 'plan.cli-readonly' positive ACCEPT {
 $p=Invoke-S1FixtureCli $cli (@('-Root',$Root,'-Mode','PlanOnly')+$selection)
 if($p.boundInputs -ne 28 -or $p.sideEffects -cne 'NONE' -or $p.vcpkgInvoked -or $p.compilerProbe -or $p.actualInstallPerformed){throw 'Plan claims'}
}
Case 'plan.no-file-or-directory-mutation' positive ACCEPT {
 if((Snapshot $Root) -cne $before -or ((@(Get-ChildItem -LiteralPath $Root -Directory -Recurse|ForEach-Object FullName) -join "`n") -cne $dirsBefore)){throw 'Plan mutation'}
}
Case 'plan.no-legacy-contracts' positive ACCEPT {
 $view=New-View no-legacy
 if(Test-Path "$view/cmake/privacy/managed-sources-v1.tsv"){throw 'Legacy present'}
 Get-S1Plan $view v2 s1-v1 ON Release x64
}
foreach($value in @('v1','unknown')){Case "selection.profile.$value" negative Profile {Assert-S1Selection $value s1-v1 ON Release x64}}
Case 'selection.missing-profile' negative ScalarString {Assert-S1Selection $null s1-v1 ON Release x64}
Case 'selection.array-profile' negative ScalarString {Assert-S1Selection @('v2') s1-v1 ON Release x64}
Case 'selection.off' negative Privacy {Assert-S1Selection v2 s1-v1 OFF Release x64}
Case 'selection.debug' negative Configuration {Assert-S1Selection v2 s1-v1 ON Debug x64}
Case 'selection.scope' negative Scope {Assert-S1Selection v2 unknown ON Release x64}
Case 'selection.architecture' negative Architecture {Assert-S1Selection v2 s1-v1 ON Release arm64}
Case 'output.override-one-empty-string-array' negative ScalarString {Get-S1ExpectedRoot $Root 'build/managed-jpegli-s1-v2' @('')}
Case 'output.override-null' negative ScalarString {Get-S1ExpectedRoot $Root 'build/managed-jpegli-s1-v2' $null}
Case 'cli.no-mode' negative ScalarString {Invoke-S1FixtureCli $cli (@('-Root',$Root)+$selection) ScalarString}
Case 'cli.no-profile' negative ScalarString {Invoke-S1FixtureCli $cli @('-Root',$Root,'-Mode','PlanOnly','-Scope','s1-v1','-Privacy','ON','-Configuration','Release','-Architecture','x64') ScalarString}
Case 'cli.explicit-install-disabled' negative InstallNotImplemented {Invoke-S1FixtureCli $cli (@('-Root',$Root,'-Mode','Install')+$selection) InstallNotImplemented}
Case 'cli.plan-cannot-request-installed' negative Mode {Invoke-S1FixtureCli $cli (@('-Root',$Root,'-Mode','PlanOnly','-InstalledDir','old')+$selection) Mode}
foreach($mutation in @('missing','extra','unregistered','case','traversal','size-type','hash-type','packages','scope','off','v1','output','unknown')){
 $expected=@{'missing'='InputCount';'extra'='InputCount';'unregistered'='InputSet';'case'='InputSet';'traversal'='RelativePath';'size-type'='InputRecord';'hash-type'='ScalarString';'packages'='Packages';'scope'='InputSelection';'off'='InputSelection';'v1'='InputSelection';'output'='OutputPolicy';'unknown'='Fields'}[$mutation]
 Case "schema.$mutation" negative $expected {
  $v=Clone-Ledger
  switch($mutation){
   missing{$v.files=$v.files[0..26]};extra{$v.files+=@($v.files[0])};unregistered{$v.files[0].path='unregistered'};case{$v.files[0].path=$v.files[0].path.ToUpperInvariant()};traversal{$v.files[0].path='../outside'}
   'size-type'{$v.files[0].size='1'};'hash-type'{$v.files[0].sha256=@($v.files[0].sha256)};packages{$v.packages[0].version='0'};scope{$v.sourceScopeId='unknown'};off{$v.shippingPrivacy='OFF'};v1{$v.profile='v1'};output{$v.outputPolicy.derived='build/f3'};unknown{$v.extra=$true}
  }
  Assert-S1SourceInputSchema $v
 }
}
foreach($mutation in @('missing','extra','size','sha','ledger','duplicate','emptydir','case','reparse')){
 $expected=@{missing='FileMissing';extra='InputSet';size='RawIdentity';sha='RawIdentity';ledger='RawIdentity';duplicate='DuplicateJsonKey';emptydir='DirectorySet';case='PathCaseAlias';reparse='ReparsePoint'}[$mutation]
 Case "physical.$mutation" negative $expected {
  $view=New-View ('physical-'+$mutation)
  $target=Join-Path $view vcpkg.json
  switch($mutation){
   missing{[IO.File]::Move($target,(Join-Path $run moved-vcpkg.json))}
   extra{[IO.File]::WriteAllText("$view/vcpkg-overlays/shipping-privacy-v2/extra",'x',$utf8)}
   size{[IO.File]::AppendAllText($target,' ',$utf8)}
   sha{$bytes=[IO.File]::ReadAllBytes($target);$bytes[0]=91;[IO.File]::WriteAllBytes($target,$bytes)}
   ledger{[IO.File]::AppendAllText("$view/cmake/privacy/source-inputs-s1-v1.json",' ',$utf8)}
   duplicate{$text=[IO.File]::ReadAllText("$view/cmake/privacy/source-inputs-s1-v1.json");$text=$text.Replace('"schemaVersion": 1,','"schemaVersion": 1,"schemaVersion": 1,');[IO.File]::WriteAllText("$view/cmake/privacy/source-inputs-s1-v1.json",$text,$utf8)}
   emptydir{[void][IO.Directory]::CreateDirectory("$view/vcpkg-overlays/shipping-privacy-v2/unknown-empty")}
   case{Resolve-S1Path $view 'VCPKG.json'}
   reparse{[void](New-Item -ItemType Junction -Path "$view/vcpkg-overlays/shipping-privacy-v2/junction" -Target $view -ErrorAction Stop)}
  }
  Read-S1SourceInputs $view
 }
}
foreach($mutation in @('generator','environment','fallback-inherit','debug','build-debug','off','old-overlay','normal-off')){
 $expected=@{generator='Generator';environment='Fields';'fallback-inherit'='Preset';debug='Configuration';'build-debug'='Configuration';off='PresetSelection';'old-overlay'='PresetSelection';'normal-off'='Preset'}[$mutation]
 Case "preset.$mutation" negative $expected {
  $v=Clone-Preset
  switch($mutation){generator{$v.configurePresets[0].generator='Ninja Multi-Config'};environment{$v.configurePresets[0].environment=@{VCPKG_ROOT='old'}};'fallback-inherit'{$v.configurePresets[1].inherits='windows-x64-release'};debug{$v.configurePresets[0].cacheVariables.CMAKE_BUILD_TYPE='Debug'};'build-debug'{$v.buildPresets[0].configuration='Debug'};off{$v.configurePresets[1].cacheVariables.CLIPSS2_SHIPPING_PRIVACY='OFF'};'old-overlay'{$v.configurePresets[1].cacheVariables.VCPKG_OVERLAY_PORTS='${sourceDir}/vcpkg-overlays'};'normal-off'{$v.configurePresets[1].name='windows-x64-release'}}
  Assert-S1Presets $v
 }
}
function Create-InstalledFixture($View){
 $installed=Join-Path $View build/vcpkg_installed-s1-v2
 foreach($relative in @('share','x64-windows-shipping-privacy/bin','x64-windows-shipping-privacy/lib','x64-windows-shipping-privacy/include','x64-windows-shipping-privacy/share','x64-windows/tools','x64-windows/share','vcpkg/info')){[void][IO.Directory]::CreateDirectory((Join-Path $installed $relative))}
 Save-Json "$installed/share/clipss2-source-profile-s1-v1.json" (Get-S1MarkerExpected $plan)
 $paragraphs=@(foreach($package in $plan.inputs.packages){$version=$package.version.Split('#');$arch=if($package.role -ceq 'target'){$plan.inputs.targetTriplet}else{$plan.inputs.hostTriplet};$port=if($version.Count -eq 2){$version[1]}else{'0'};"Package: $($package.name)`nVersion: $($version[0])`nPort-Version: $port`nArchitecture: $arch`nStatus: install ok installed"})
 [IO.File]::WriteAllText("$installed/vcpkg/status",(($paragraphs -join "`n`n")+"`n"),$utf8)
 return $installed
}
$installed=Create-InstalledFixture $Root
Case 'marker.fixture-logic-only' positive ACCEPT {$r=Test-S1Installed $Root $installed $plan;if($r.actualInstallPerformed -or $r.abiVerified -or $r.binaryIdentityVerified){throw 'False install claim'}}
Case 'marker.cli-logic-only' positive ACCEPT {$r=Invoke-S1FixtureCli $cli (@('-Root',$Root,'-Mode','ValidateInstalled','-InstalledDir',$installed)+$selection);if(!$r.markerValidated -or $r.actualInstallPerformed){throw 'Marker result'}}
foreach($mutation in @('hash','old-id','array','unknown','false-install-claim','schema-type')){
 $expected=if($mutation -cin @('unknown','false-install-claim')){'Fields'}elseif($mutation -ceq 'array'){'ScalarString'}else{'MarkerIdentity'}
 Case "marker.$mutation" negative $expected {$v=Get-S1MarkerExpected $plan;switch($mutation){hash{$v.sourceInputsSha256='0'*64};'old-id'{$v.markerId='clipss2-shipping-profile-v2'};array{$v.profile=@('v2')};unknown{$v.extra=1};'false-install-claim'{$v.actualInstallPerformed=$true};'schema-type'{$v.schemaVersion='1'}};Assert-S1Marker $v $plan}
}
Case 'marker.old-root' negative OutputRoot {Test-S1Installed $Root (Join-Path $Root build/f3) $plan}
Case 'marker.old-file-only' negative FileMissing {$v=New-View old-marker;$i=Create-InstalledFixture $v;[IO.File]::Move("$i/share/clipss2-source-profile-s1-v1.json","$i/share/clipss2-shipping-profile-v2.json");Test-S1Installed $v $i $plan}
Case 'marker.status-mismatch' negative InstalledStatus {$v=New-View wrong-status;$i=Create-InstalledFixture $v;[IO.File]::WriteAllText("$i/vcpkg/status",'Package: unknown',$utf8);Test-S1Installed $v $i $plan}
Case 'marker.missing-structure' negative InstalledStructure {$v=New-View wrong-layout;[void][IO.Directory]::CreateDirectory("$v/build/vcpkg_installed-s1-v2/share");Save-Json "$v/build/vcpkg_installed-s1-v2/share/clipss2-source-profile-s1-v1.json" (Get-S1MarkerExpected $plan);Test-S1Installed $v "$v/build/vcpkg_installed-s1-v2" $plan}
Case 'plan.existing-root-is-not-admitted' positive ACCEPT {$p=Get-S1Plan $Root v2 s1-v1 ON Release x64;if(@($p.outputRoots|Where-Object state -CEQ PRESENT_UNADMITTED).Count -lt 1 -or $p.installedValidated){throw 'Reuse claim'}}

function Cmake-Harness($Name,$Mutation='',$InvokeProfile=$false,$InvokeCallback=$false){
 $rootSlash=$Root.Replace('\','/');$originalSlash=$OriginalRoot.Replace('\','/')
 $lines=@('cmake_minimum_required(VERSION 3.28)',('set(CMAKE_SOURCE_DIR "'+$rootSlash+'")'),'set(CMAKE_GENERATOR "Ninja")','set(CMAKE_BUILD_TYPE "Release")','set(MSVC TRUE)','set(CMAKE_CXX_COMPILER_ID "MSVC")','set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID "x64")','set(CMAKE_SIZEOF_VOID_P 8)','set(CLIPSS2_SHIPPING_PRIVACY "ON")','set(CLIPSS2_SHIPPING_PROFILE "v2")','set(CLIPSS2_PUBLICATION_SCOPE "s1-v1")','set(VCPKG_TARGET_TRIPLET "x64-windows-shipping-privacy")','set(VCPKG_HOST_TRIPLET "x64-windows")','set(VCPKG_MANIFEST_INSTALL "OFF")','set(CMAKE_FIND_USE_PACKAGE_REGISTRY "OFF")','set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY "OFF")')
 foreach($pair in @('CMAKE_BINARY_DIR|build/windows-x64-s1-v2','VCPKG_INSTALLED_DIR|build/vcpkg_installed-s1-v2','CLIPSS2_DERIVED_JPEGLI_DIR|build/managed-jpegli-s1-v2','CLIPSS2_JPEGLI_SOURCE_DIR|build/managed-jpegli-src','VCPKG_OVERLAY_PORTS|vcpkg-overlays/shipping-privacy-v2','VCPKG_OVERLAY_TRIPLETS|triplets','CMAKE_TOOLCHAIN_FILE|build/shipping-vcpkg-s1-v2/scripts/buildsystems/vcpkg.cmake')){$parts=$pair.Split('|');$lines+=('set('+$parts[0]+' "'+$rootSlash+'/'+$parts[1]+'")')}
 if($Mutation){$lines+=$Mutation}
 $lines+=('include("'+$rootSlash+'/cmake/ShippingPrivacy.cmake")');$lines+='clipss2_validate_s1_configuration()'
 if($InvokeProfile){$lines+='clipss2_validate_shipping_profile_s1()'}
 if($InvokeCallback){$lines+=('clipss2_validate_privacy_derivative("'+$originalSlash+'" "'+$rootSlash+'/build/managed-jpegli-s1-v2")')}
 $path=Join-Path $run ($Name+'.cmake');[IO.File]::WriteAllText($path,(($lines -join "`n")+"`n"),$utf8)
 $r=Exec 'cmake' @('-P',$path)
 if($r.code -ne 0){if($r.stderr -match 'S1:([A-Za-z0-9_:]+)'){Stop-S1 $Matches[1]};throw $r.stderr}
}
Case 'cmake.script-config-single-ninja-release' positive ACCEPT {Cmake-Harness cmake-positive}
Case 'cmake.script-policy-to-real-cli-fixture-marker' positive ACCEPT {Cmake-Harness cmake-cli '' $true}
foreach($entry in @(
 @('missing','unset(CLIPSS2_SHIPPING_PROFILE)','Profile'),@('v1','set(CLIPSS2_SHIPPING_PROFILE v1)','Profile'),@('off','set(CLIPSS2_SHIPPING_PRIVACY OFF)','Privacy'),@('scope','set(CLIPSS2_PUBLICATION_SCOPE unknown)','Scope'),@('debug','set(CMAKE_BUILD_TYPE Debug)','Configuration'),@('multi','set(CMAKE_GENERATOR "Ninja Multi-Config")','Generator'),@('vs','set(CMAKE_GENERATOR "Visual Studio 17 2022")','Generator'),@('types','set(CMAKE_CONFIGURATION_TYPES Release)','Generator'),@('arm64','set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID ARM64)','Architecture'),@('32bit','set(CMAKE_SIZEOF_VOID_P 4)','Architecture'),@('clang','set(CMAKE_CXX_COMPILER_ID Clang)','Architecture'),@('old-installed','set(VCPKG_INSTALLED_DIR "old")','OutputRoot:VCPKG_INSTALLED_DIR'),@('old-overlay','set(VCPKG_OVERLAY_PORTS "old")','OutputRoot:VCPKG_OVERLAY_PORTS'),@('toolchain','set(CMAKE_TOOLCHAIN_FILE "old")','OutputRoot:CMAKE_TOOLCHAIN_FILE'),@('triplet','set(VCPKG_TARGET_TRIPLET x64-windows)','DependencyPolicy'))){
 Case ('cmake.reject.'+$entry[0]) negative $entry[2] {Cmake-Harness ('cmake-'+$entry[0]) $entry[1]}
}
Case 'derive.preexisting-output-refused' negative OutputExists {Invoke-S1FixtureCli $derive (@('-Root',$Root,'-Mode','Derive','-Original',$OriginalRoot)+$selection) OutputExists}
Case 'derive.old-output-root' negative OutputRoot {Invoke-S1FixtureCli $derive (@('-Root',$Root,'-Mode','VerifyOnly','-Original',$OriginalRoot,'-Destination',(Join-Path $Root build/managed-jpegli-shipping-v2))+$selection) OutputRoot}
Case 'derive.ancestral-root-rejected' negative OriginalRoot {Invoke-S1FixtureCli $derive (@('-Root',$Root,'-Mode','VerifyOnly','-Original',(Resolve-Path "$EvidenceRoot/../..").Path)+$selection) OriginalRoot}
Case 'derive.real-pin-mismatch' negative CheckoutRevision {Test-S1ManagedOriginal $Root (Resolve-Path "$EvidenceRoot/../..").Path}
foreach($storage in @('alternates','commondir')){
 Case "derive.real-layout-$storage" negative SharedGit {
  $o=Join-Path $run ('original-'+$storage);[void][IO.Directory]::CreateDirectory("$o/.git/objects/info")
  $file=if($storage -ceq 'commondir'){"$o/.git/commondir"}else{"$o/.git/objects/info/alternates"};[IO.File]::WriteAllText($file,'untrusted',$utf8)
  Test-S1ManagedOriginal $Root $o
 }
}
Case 'derive.dirty-original-forwarding-unit' negative DirtyCheckout {
 function Test-S1OriginalSource {Stop-S1 DirtyCheckout}
 Test-S1ManagedOriginal $Root $OriginalRoot
}
Case 'derive.accepted-postimage' positive ACCEPT {Test-S1ManagedDerivative $Root (Join-Path $Root build/managed-jpegli-s1-v2)}
Case 'derive.verifyonly-cli' positive ACCEPT {
 $r=Invoke-S1FixtureCli $derive (@('-Root',$Root,'-Mode','VerifyOnly','-Original',$OriginalRoot,'-Destination',(Join-Path $Root build/managed-jpegli-s1-v2))+$selection)
 if(!$r.derivedValidated -or $r.writes -or $r.build){throw 'VerifyOnly claims'}
}
Case 'cmake.callback-to-verifyonly-cli' positive ACCEPT {Cmake-Harness cmake-callback '' $false $true}
$badDerived=Join-Path $run corrupt-derived
[void][IO.Directory]::CreateDirectory($badDerived)
foreach($row in $plan.managed.rows){$dest=Join-Path $badDerived $row.path;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest));[IO.File]::Copy((Join-Path "$Root/build/managed-jpegli-s1-v2" $row.path),$dest,$false)}
Case 'derive.postimage-mismatch' negative DerivedPostimage {[IO.File]::AppendAllText((Join-Path $badDerived 'third_party/highway/hwy/base.h'),' ',$utf8);Test-S1ManagedDerivative $Root $badDerived}
Case 'derive.unregistered-empty-directory' negative DirectorySet {[void][IO.Directory]::CreateDirectory("$badDerived/unregistered-empty");Test-S1ManagedDerivative $Root $badDerived}
$notTested.Add('Physical file symlink: B1 Windows privilege limitation retained; junction tested here.')
$notTested.Add('Physically dirty upstream checkout: forwarding unit fixture, original preserved.')
$notTested.Add('Actual generator/MSVC/compiler/configure/install/build/ABI/runtime: NOT PERFORMED; CMake -P uses explicit metadata fixtures.')
$summary=[ordered]@{result=$(if(@($results|Where-Object result -CEQ FAIL).Count){'FAIL'}else{'PASS'});positive=@($results|Where-Object kind -CEQ positive).Count;negative=@($results|Where-Object kind -CEQ negative).Count;failed=@($results|Where-Object result -CEQ FAIL).Count;cases=@($results);notTested=@($notTested);cmakeMode='SCRIPT -P ONLY, no project/configure/compiler';derivePerformedBySuite=$false;actualInstall=$false}
[IO.File]::WriteAllText("$run/results.json",($summary|ConvertTo-Json -Depth 10),$utf8)
[ordered]@{receipt="$run/results.json";result=$summary.result;positive=$summary.positive;negative=$summary.negative;failed=$summary.failed;failures=@($results|Where-Object result -CEQ FAIL)}|ConvertTo-Json -Depth 7
if($summary.failed){exit 1}
