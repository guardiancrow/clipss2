# Copyright (c) 2026 guardiancrow. MIT.
"""Verify the built Release edges and import archives used by SEC-MAINT-3D."""

import hashlib
import json
from pathlib import Path
import re
import struct
import sys

TRIPLET = "x64-windows-shipping-privacy"
WINDOWS_LIBS = set(
    "kernel32 user32 gdi32 winspool shell32 ole32 oleaut32 uuid comdlg32 advapi32".split()
)
TARGETS = (
    ("libjpeg-turbo", "turbojpeg", "turbojpeg", {"spng"}),
    ("libjpeg-turbo", "jpeg62", "jpeg", set()),
    ("libspng", "spng", "spng", {"libz"}),
    ("libyuv", "libyuv", "libyuv", {"jpeg"}),
    ("libavif", "avif", "avif", {"aom", "libyuv"}),
)


def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest().upper()


def check(condition, message):
    if not condition:
        raise ValueError(message)


def import_archive(path, dll):
    archive = path.read_bytes()
    check(archive.startswith(b"!<arch>\n"), "invalid COFF archive")
    entries = 0
    offset = 8
    while offset < len(archive):
        header = archive[offset:offset + 60]
        size = int(header[48:58])
        body = archive[offset + 60:offset + 60 + size]
        name = header[:16].decode("ascii").strip()
        check(len(body) == size and header[58:60] == b"`\n", "invalid archive member")
        offset += 60 + size + (size & 1)
        if name in {"/", "//"}:
            continue
        check(name == dll + "/", "archive contains a non-import member")
        if body.startswith(b"\0\0\xff\xff"):
            check(struct.unpack_from("<H", body, 6)[0] == 0x8664, "non-x64 import")
        else:
            machine, sections = struct.unpack_from("<HH", body)
            check(machine == 0x8664 and sections > 0, "invalid COFF import descriptor")
            start = 20 + struct.unpack_from("<H", body, 16)[0]
            for n in range(sections):
                section = body[start + n * 40:start + n * 40 + 8].rstrip(b"\0")
                check(section.startswith(b".idata$") or section == b".debug$S",
                      "archive contains non-import code")
        entries += 1
    check(entries and offset == len(archive), "unexpected import archive shape")
    return entries


def verify(run):
    install = run / "installed" / TRIPLET
    results = []
    for port, stem, name, wanted in TARGETS:
        build = run / "buildtrees" / port / f"{TRIPLET}-rel"
        graph = (build / "build.ninja").read_text(encoding="utf-8").replace("\\", "/")
        graph = re.sub(r"\$\n[ \t]*", "", graph)
        edge = re.search(rf"(?m)^build {re.escape(stem)}\.dll [^\n]+\n(?:  [^\n]+\n)*", graph)
        check(edge is not None, f"{port}: Release link edge missing")
        edge = edge.group()
        objects = sorted(set(re.findall(r"[\w./-]+\.obj\b", edge)))
        check(objects, f"{port}: link has no object inputs")
        built = {row.split("\t")[3].replace("\\", "/")
                 for row in (build / ".ninja_log").read_text().splitlines()
                 if row and not row.startswith("#")}
        check(set(objects) <= built, f"{port}: incomplete object build evidence")
        libraries = next(row for row in edge.splitlines() if row.startswith("  LINK_LIBRARIES ="))
        names = set(re.findall(r"([\w.-]+)\.lib\b", libraries, re.IGNORECASE))
        check(names - WINDOWS_LIBS == wanted, f"{port}: unexpected library inputs")
        for dependency in wanted:
            path = (install / "lib" / f"{dependency}.lib").as_posix()
            check(path in libraries.replace("$:", ":"), f"{port}: dependency outside 3D install")
        check(not re.search(r"(?:spng-static|zlib-static|\bminiz[^ /]*\.obj|/spng/zlib/)", graph, re.I),
              f"{port}: bundled compression input")
        if port == "libjpeg-turbo":
            check(not re.search(r"(?:/spng/spng\.c(?:\b|\.)|/spng\.c\.obj)", graph, re.I),
                  "TurboJPEG builds vendored spng")
        if port == "libspng":
            check(len(objects) == 1 and objects[0].endswith("/spng/spng.c.obj") and
                  not re.search(r"[-/]D\s*SPNG_USE_MINIZ\b", graph),
                  "libspng does not build the system-zlib path")
        dll = f"{stem}.dll"
        library = install / "lib" / f"{name}.lib"
        results.append(dict(port=port, dll=dll, sha256=digest(install / "bin" / dll),
                            objects=objects, linkLibraries=sorted(names),
                            importLibrary=library.name, importLibrarySha256=digest(library),
                            importMembers=import_archive(library, dll),
                            spngStaticMacro=bool(re.search(r"[-/]DSPNG_STATIC\b", graph))))
    zlib = install / "lib/libz.lib"
    return dict(
        schemaVersion=1, result="PASS", triplet=TRIPLET, configuration="Release",
        binaries=results,
        zlib=dict(sha256=digest(install / "bin/libz.dll"), importLibrarySha256=digest(zlib),
                  importMembers=import_archive(zlib, "libz.dll")),
        pngSha256=digest(install / "bin/libpng16.dll"),
        excludedObjects=["TurboJPEG vendored spng.c", "TurboJPEG vendored zlib", "libspng miniz"],
    )


if __name__ == "__main__":
    try:
        root = Path(sys.argv[1]).resolve(strict=True)
        evidence = verify(root)
        with (root / "external-link-evidence.json").open("x", encoding="utf-8", newline="\n") as f:
            json.dump(evidence, f, indent=2)
            f.write("\n")
    except (ValueError, OSError, StopIteration, struct.error) as error:
        message = str(error) if isinstance(error, ValueError) else type(error).__name__
        raise SystemExit(f"SEC-MAINT-3D: {message}") from None
