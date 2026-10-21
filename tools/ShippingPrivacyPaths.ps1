#requires -Version 7.2
# Copyright (c) 2026 guardiancrow. MIT.
function Normalize-Absolute([string]$Value) {
    if (![IO.Path]::IsPathFullyQualified($Value) -or $Value.StartsWith('\\?\') -or $Value.StartsWith('\\.\')) { throw 'Fully qualified filesystem path required' }
    $full = [IO.Path]::GetFullPath($Value.Replace('/', '\'))
    $volume = [IO.Path]::GetPathRoot($full)
    if ($full.Length -gt $volume.Length) { return $full.TrimEnd([char]92) }
    return $full
}
function Reject-ReparseAncestors([string]$FullPath) {
    $cursor = Normalize-Absolute $FullPath
    while ($cursor) {
        try {
            if (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse boundary rejected' }
        } catch [IO.FileNotFoundException] {} catch [IO.DirectoryNotFoundException] {}
        $parentPath = [IO.Path]::GetDirectoryName($cursor)
        if (!$parentPath -or $parentPath -eq $cursor) { break }
        $cursor = $parentPath
    }
}
function Resolve-Owned([string]$Root, [string]$Relative) {
    $canonical = Normalize-Absolute $Root
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '[:*?"<>|]' -or $Relative.Contains([char]0)) { throw 'Unsafe relative path' }
    $resolved = Normalize-Absolute ([IO.Path]::GetFullPath($Relative.Replace('/', '\'), $canonical))
    $relativePath = [IO.Path]::GetRelativePath($canonical, $resolved)
    if ([IO.Path]::IsPathRooted($relativePath) -or $relativePath -eq '..' -or $relativePath.StartsWith('..\', [StringComparison]::Ordinal)) { throw 'Root traversal rejected' }
    Reject-ReparseAncestors $canonical
    Reject-ReparseAncestors $resolved
    return $resolved
}
function Get-ShippingFiles([string]$Root, [string[]]$GitDirectories = @()) {
    $rootPath = Normalize-Absolute $Root
    Reject-ReparseAncestors $rootPath
    $files = [Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::Ordinal)
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue($rootPath)
    while ($queue.Count) {
        foreach ($entry in [IO.DirectoryInfo]::new($queue.Dequeue()).EnumerateFileSystemInfos()) {
            $rel = [IO.Path]::GetRelativePath($rootPath, $entry.FullName).Replace('\', '/')
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse entry rejected' }
            if (!$seen.Add($rel)) { throw 'Case-colliding source path' }
            if ($rel -cin $GitDirectories) {
                if (($entry.Attributes -band [IO.FileAttributes]::Directory) -eq 0) { throw 'Independent checkout directory required' }
                continue
            }
            if (($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0) { $queue.Enqueue($entry.FullName) }
            else { $files.Add($rel, $entry.FullName) }
        }
    }
    return ,$files
}
