#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
param([Parameter(Mandatory)][string]$AssetRoot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore
$assetRoot = (Resolve-Path -LiteralPath $AssetRoot).Path
if (Test-Path -LiteralPath (Join-Path $assetRoot 'notes/windows-validation.json')) {
    throw 'Windows validation output already exists; preserve it and use a fresh generated tree.'
}
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$icoPath = Join-Path $assetRoot 'ico/clipss2.ico'
$options = [System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat
$cache = [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
$decoder = [System.Windows.Media.Imaging.IconBitmapDecoder]::new([Uri]::new($icoPath), $options, $cache)
if ($decoder.Frames.Count -ne $sizes.Count) { throw 'ICO frame count mismatch' }
$results = foreach ($size in $sizes) {
    $pngUri = [Uri]::new((Join-Path $assetRoot "png/clipss2-$size.png"))
    $png = [System.Windows.Media.Imaging.BitmapDecoder]::Create($pngUri, $options, $cache).Frames[0]
    $frames = @($decoder.Frames | Where-Object { $_.PixelWidth -eq $size -and $_.PixelHeight -eq $size })
    if ($frames.Count -ne 1) { throw "ICO size missing or duplicated: $size" }
    $hashes = foreach ($image in @($png, $frames[0])) {
        if ($image.PixelWidth -ne $size -or $image.PixelHeight -ne $size) { throw "Dimension mismatch: $size" }
        if ($image.Format.ToString() -ne 'Bgra32') { throw "Unexpected pixel format: $($image.Format)" }
        $pixels = [byte[]]::new($size * $size * 4)
        $image.CopyPixels($pixels, $size * 4, 0)
        foreach ($y in 0..($size - 1)) {
            foreach ($x in 0..($size - 1)) {
                if ($x -eq 0 -or $y -eq 0 -or $x -eq ($size - 1) -or $y -eq ($size - 1)) {
                    if ($pixels[($y * $size + $x) * 4 + 3] -ne 0) { throw "Opaque outer perimeter: $size" }
                }
            }
        }
        $centerIndex = ([int]($size / 2) * $size + [int]($size / 2)) * 4
        if ($pixels[$centerIndex + 3] -ne 255 -or $pixels[$centerIndex + 2] -gt 30 -or $pixels[$centerIndex + 1] -gt 90) { throw "Unexpected center: $size" }
        [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($pixels))
    }
    if ($hashes[0] -ne $hashes[1]) { throw "PNG / ICO decoded pixel mismatch: $size" }
    [pscustomobject]@{
        size = $size
        pngDecoded = $true
        icoFrameDecoded = $true
        decodedPixelsMatch = $true
        perimeterTransparent = $true
        centerOpaqueBlue = $true
    }
}
$report = [ordered]@{
    result = 'PASS'
    decoder = 'Windows WPF BitmapDecoder / IconBitmapDecoder (all seven frames including 256)'
    sizes = @($results)
    limitation = 'Image decoding only. Shell display and actual monitor DPI require separate acceptance.'
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $assetRoot 'notes/windows-validation.json') -Encoding utf8
$report | ConvertTo-Json -Depth 5
