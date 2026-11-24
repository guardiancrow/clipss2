# Copyright (c) 2026 guardiancrow. MIT.
"""Verify the pinned archive tree after the overlay patches and link restoration."""

import hashlib
import os
from pathlib import Path, PurePosixPath
import stat
import sys
import tarfile

# SHA-256 after the baseline vcpkg helper applies the three patches.
POSTIMAGES = {
    "third_party/CMakeLists.txt": "d47ae3ea953d8d78c851b4a60030c7adaca56846144487f21c6591e8dcb4e011",
    "lib/jxl/base/status.h": "ea06c705ba47c81ec742f17e6c9b9a2d2be8e62586226d2c0a60a8b01dd37327",
    "lib/jxl/decode.cc": "72b2137ca24d393c94070429ecb98aa9598c9bc40b44930e4d0ba344d77364e7",
    "lib/jxl/encode.cc": "9053f4d2fc53caff07089156124790d8da2ec8df785b7c79c00d22fe17fa64dc",
}


def verify(archive, root):
    expected = set()
    patched = set()
    regular = directories = links = 0
    with tarfile.open(archive, "r:gz") as source:
        for entry in source:
            name = PurePosixPath(entry.name)
            if name.parts == ("libjxl-0.13.0",) and entry.isdir():
                continue
            if (len(name.parts) < 2 or name.parts[0] != "libjxl-0.13.0"
                    or ".." in name.parts or "\\" in entry.name or ":" in entry.name):
                raise ValueError("unexpected archive path")
            relative = PurePosixPath(*name.parts[1:]).as_posix()
            if relative in expected:
                raise ValueError(f"duplicate archive entry: {relative}")
            expected.add(relative)
            path = root / relative
            if path.is_junction():
                raise ValueError(f"unexpected junction: {relative}")
            mode = path.lstat().st_mode
            if entry.isdir() and stat.S_ISDIR(mode):
                directories += 1
            elif entry.isfile() and stat.S_ISREG(mode):
                regular += 1
                if relative in POSTIMAGES:
                    wanted = POSTIMAGES[relative]
                    patched.add(relative)
                else:
                    with source.extractfile(entry) as content:
                        wanted = hashlib.file_digest(content, "sha256").hexdigest()
                with path.open("rb") as content:
                    if hashlib.file_digest(content, "sha256").hexdigest() != wanted:
                        raise ValueError(f"file digest mismatch: {relative}")
            elif entry.issym() and stat.S_ISLNK(mode):
                links += 1
                target = os.readlink(path)
                if (target != entry.linkname or target != "iqa_wrapper.sh"
                        or Path(target).is_absolute()
                        or not path.resolve(strict=True).is_relative_to(root)
                        or not path.resolve(strict=True).is_file()):
                    raise ValueError(f"symbolic link target mismatch: {relative}")
            else:
                raise ValueError(f"archive entry type mismatch: {relative}")

    actual = {
        (Path(directory) / name).relative_to(root).as_posix()
        for directory, subdirs, files in os.walk(root, followlinks=False)
        for name in subdirs + files
    }
    if actual != expected:
        raise ValueError("unexpected extracted entries: " + ", ".join(sorted(actual - expected)))
    if links != 12 or patched != POSTIMAGES.keys():
        raise ValueError("unexpected link or patched file count")
    print(f"SEC-MAINT-3A archive verified: {regular} regular files, "
          f"{directories} directories, {links} relative symlinks, {len(patched)} patched files")


if __name__ == "__main__":
    try:
        verify(Path(sys.argv[1]), Path(sys.argv[2]).resolve(strict=True))
    except (OSError, ValueError, tarfile.TarError) as error:
        if isinstance(error, OSError):
            raise SystemExit(f"SEC-MAINT-3A archive verification I/O failure ({error.errno})") from None
        raise SystemExit(f"SEC-MAINT-3A: {error}") from None
