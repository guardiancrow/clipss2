param([string]$SourceCache = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $repo 'build/managed-jpegli-src'
function Run([string[]]$Arguments) {
    & git --no-optional-locks -c core.autocrlf=false @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git failed: $Arguments" }
}
function Checkout([string]$Path, [string]$Url, [string]$Revision, [string]$Cache) {
    if (!(Test-Path (Join-Path $Path '.git'))) {
        if ($Cache) { Run @('clone','--no-hardlinks','--no-checkout',$Cache,$Path); Run @('-C',$Path,'remote','set-url','origin',$Url) }
        else { Run @('clone','--filter=blob:none','--no-checkout',$Url,$Path); Run @('-C',$Path,'fetch','--depth','1','origin',$Revision) }
        Run @('-C',$Path,'checkout','--detach',$Revision)
    }
    $actual = & git -C $Path rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $Revision) { throw "Unexpected revision in $Path; preserved" }
    $dirty = & git -C $Path status --porcelain --untracked-files=no
    if ($LASTEXITCODE -ne 0 -or $dirty) { throw "Modified checkout $Path; preserved" }
}
Checkout $destination 'https://github.com/google/jpegli.git' '031a0077f5799a6041004267fc12b956c1f52a20' $SourceCache
foreach ($dependency in @(
    @('highway','https://github.com/google/highway.git','271a9a0ed9de1232d9117f1572c3fe28f8542ec1'),
    @('skcms','https://skia.googlesource.com/skcms','96d9171c94b937a1b5f0293de7309ac16311b722'),
    @('libjpeg-turbo','https://github.com/libjpeg-turbo/libjpeg-turbo.git','8ecba3647edb6dd940463fedf38ca33a8e2a73d1'))) {
    $cache = if ($SourceCache) { Join-Path $SourceCache "third_party/$($dependency[0])" } else { '' }
    Checkout (Join-Path $destination "third_party/$($dependency[0])") $dependency[1] $dependency[2] $cache
}
Write-Output 'Pinned product jpegli sources ready; normal configure/build needs no upstream network.'
