# v0.1.3 を公開ソースから構築する

v0.1.3 の通常構築はリポジトリ直下の manifest と overlay を使います。Windows x64、Ninja 単一構成、Release /MD、Shipping Privacy v2 が対象です。製品依存 DLL は動的にリンクし、Microsoft VC++ Runtime x64 は外部導入とします。

## ツールと作業領域

Visual Studio Build Tools 2022 v143、MSVC ツールセット 14.44.35207（コンパイラー 19.44.35230.0）、Windows SDK 10.0.26100.0、CMake 4.4.4、Ninja 1.13.2、PowerShell 7.7.0、Git for Windows 2.56.1.windows.1、Python 3.11 以降を用意してください。vcpkg の固定ツールと実行ファイルのハッシュは [ツールチェーン台帳](../cmake/security/sec-maint-v013/toolchain-v1.json) にあります。libjxl のアーカイブには相対シンボリックリンクが含まれます。一般ユーザーでリンクを作成できる環境を使用してください。展開でリンクを復元できなければ構築を止めます。

以下は同じ PowerShell でリポジトリのルートから実行します。承認済み x64 開発者環境を用意し、新しい run 名を選んでください。

~~~powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products Microsoft.VisualStudio.Product.BuildTools -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$vars = & cmd.exe /d /c ('call "' + $vcvars + '" x64 10.0.26100.0 -vcvars_ver=14.44.35207 >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed' }
foreach ($line in $vars) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
$root = (Get-Location).Path
$env:CLIPSS2_RELEASE_RUN = 'public-v013-01'
$run = Join-Path $root ('build/sec-maint-v013/5/' + $env:CLIPSS2_RELEASE_RUN)
if (Test-Path -LiteralPath $run) { throw 'Choose an unused run name' }
New-Item -ItemType Directory -Path $run
$registry = Join-Path $run 'vcpkg'
$pin = Get-Content cmake/security/sec-maint-v013/toolchain-v1.json -Raw | ConvertFrom-Json

git -c core.autocrlf=false init --initial-branch=main $registry
git -c core.autocrlf=false -C $registry fetch --no-tags --depth=1 https://github.com/microsoft/vcpkg.git $pin.vcpkgBaseline
if ($LASTEXITCODE -ne 0) { throw 'vcpkg fetch failed' }
git -c core.autocrlf=false -C $registry checkout --detach $pin.vcpkgBaseline
if ($LASTEXITCODE -ne 0) { throw 'vcpkg checkout failed' }
& (Join-Path $registry 'bootstrap-vcpkg.bat') -disableMetrics
if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }
$tool = Join-Path $registry 'vcpkg.exe'
if ((Get-FileHash -LiteralPath $tool).Hash -cne $pin.vcpkgToolSha256) { throw 'vcpkg SHA-256 mismatch' }
& $tool version
~~~

## 固定ソースと依存

AOM と libyuv は、上流の固定 Git commit から vcpkg が使うアーカイブを再生成します。次のツールは新しい bare リポジトリに公式の Git リビジョンを取得し、commit、tree、アーカイブ内容を照合します。Git for Windows 2.56.1.windows.1 で使うコマンドは **git -c core.autocrlf=false archive <commit> -o <archive>.tar.gz** です。ファイル名、SHA-512、ファイル台帳 SHA-256、固定 commit／tree は [承認済み入力](../cmake/security/sec-maint-v013/3f/source-inputs-v1.json) にあります。生成バイトが一致した場合にだけ run の downloads へ配置します。Gitiles の HTTP アーカイブでは同じ commit でも配信 SHA-512 が変わる場合があります。

~~~powershell
python -B tools/prepare_sec_maint_git_archives.py --run $run
if ($LASTEXITCODE -ne 0) { throw 'Pinned archives could not be reproduced' }
$env:VCPKG_ROOT = $registry
$env:VCPKG_DOWNLOADS = Join-Path $run 'downloads'
$env:VCPKG_DISABLE_METRICS = '1'
$env:VCPKG_BINARY_SOURCES = 'clear'
$env:X_VCPKG_ASSET_SOURCES = 'clear'
$env:TEMP = Join-Path $run 'tmp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Path $env:TEMP
$installArgs = @('install', "--x-manifest-root=$root", "--x-install-root=$run/installed",
    "--x-buildtrees-root=$run/buildtrees", "--x-packages-root=$run/packages",
    "--overlay-triplets=$root/triplets", '--triplet=x64-windows-shipping-privacy',
    '--host-triplet=x64-windows', '--disable-metrics')
& $tool @installArgs
if ($LASTEXITCODE -ne 0) { throw 'vcpkg install failed' }

./tools/prepare_jpegli.ps1 -Destination (Join-Path $run 'managed-jpegli-src')
./tools/derive_shipping_privacy_s1.ps1 -Mode Derive -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64 -ReleaseRun $run
./tools/validate_sec_maint_3a_install.ps1 -Mode Seal -Phase 5 -RunRoot $run -ReferenceInstalled (Join-Path $run 'installed')
~~~

[ルートの overlay 順序](../vcpkg-configuration.json) により、保守更新用 port、Brotli 保守用 port、Shipping Privacy v2 port、固定 registry の順で解決します。TurboJPEG の PNG loader は共有 libspng、libspng と libpng は共有 zlib 1.4 を使用します。更新した port と backport の由来は [SOURCES](../licenses/SOURCES.md)、依存の版・機能・ABI は [v0.1.3 入力台帳](../cmake/security/sec-maint-v013/5/source-inputs-v1.json) を参照してください。

## 構築、試験、配布構成

~~~powershell
cmake --preset windows-x64-shipping-v2
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build --preset windows-x64-shipping-v2
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
Copy-Item -LiteralPath packaging/clipss2.portable -Destination (Join-Path $run 'build/clipss2.portable')
ctest --preset windows-x64-shipping-v2
~~~

通常プリセットは公開ソースから構築できる回帰試験を登録します。過去の候補を比較対象とする 21 試験を含む正式候補の全 62 件は、同じプリセットで CLIPSS2_MAINT_HISTORICAL_TESTS を有効にし、固定済みの比較入力を検証する [正式候補構築ツール](../tools/build_sec_maint_3a.ps1) の Phase 5 で実行します。

試用時は build 下の clipss2.exe、隣接 DLL 18 本と clipss2.portable をまとめて使用します。配布物のファイル一覧は [v0.1.3 プロファイル](../packaging/public/profile-sec-maint-v013-5.json) を参照してください。正式候補構築ツールが発行した BUILD-IDENTITY と SBOM を含む候補は、**python -B tools/package_v013.py --run <run>** で検証用 ZIP にまとめられます。固定ソースと依存構成の再現可能性、環境の異なるビルド間での PE のバイト一致は別に確認します。
