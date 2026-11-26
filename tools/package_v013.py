# Copyright (c) 2026 guardiancrow. MIT.
"""Assemble the sealed v0.1.3 Portable candidate in an isolated run."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import zipfile


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def read(path):
    return json.loads(path.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    run = args.run.resolve(strict=True)
    allowed = (root / "build/sec-maint-v013/5").resolve(strict=True)
    if run == allowed or not run.is_relative_to(allowed):
        parser.error("Expected a new SEC-MAINT-5 run directory")
    profile_path = root / "packaging/public/profile-sec-maint-v013-5.json"
    inputs = root / "cmake/security/sec-maint-v013/5/source-inputs-v1.json"
    identity_path = run / "BUILD-IDENTITY.json"
    sbom_path = run / "SBOM.cdx.json"
    profile, identity, sbom = map(read, (profile_path, identity_path, sbom_path))
    payload = run / "product"
    if (identity["scope"] != "sec-maint-v013-5" or identity["version"] != "0.1.3"
            or identity["profileSha256"] != sha(profile_path)
            or identity["sourceInputsSha256"] != sha(inputs)
            or identity["releaseApproved"] or identity["runtimeBundled"]):
        raise ValueError("Unsealed or unexpected build identity")
    receipt = run / "installed/share/clipss2-sec-maint-5-install-receipt-v1.json"
    marker = run / "installed/share/clipss2-sec-maint-5-installed-v1.json"
    if (identity["installedReceiptSha256"] != sha(receipt)
            or identity["installedMarkerSha256"] != sha(marker)
            or read(marker)["receiptSha256"].upper() != sha(receipt)):
        raise ValueError("Dependency receipt mismatch")
    bindings = {p["name"]: p["value"] for p in sbom["metadata"]["properties"]}
    if bindings["clipss2:buildIdentitySha256"] != sha(identity_path):
        raise ValueError("SBOM build identity mismatch")

    members = {}
    review = []
    for row in identity["sourceBindings"]:
        file = root / row["path"]
        if file.stat().st_size != row["size"] or sha(file) != row["sha256"]:
            raise ValueError(f"Source input changed: {row['path']}")
    bound = {row["path"] for row in identity["sourceBindings"]}
    expected = {"clipss2.exe", *profile["productRuntimeDlls"]}
    if len(expected) != 19 or {p.name for p in payload.iterdir()} != expected:
        raise ValueError("Portable binary set mismatch")
    if {row["path"] for row in identity["binaries"]} != expected:
        raise ValueError("Build identity binary set mismatch")
    for row in identity["binaries"]:
        source = payload / row["path"]
        if source.stat().st_size != row["size"] or sha(source) != row["sha256"]:
            raise ValueError(f"Binary differs from build identity: {row['path']}")
        members[row["path"]] = source
    for row in profile["documents"]:
        name, relative = row["path"], row["source"]
        if not isinstance(name, str) or PurePosixPath(name).as_posix() != name or "\\" in name \
                or name.startswith("/") or any(part in ("", ".", "..") for part in name.split("/")) \
                or name.casefold() in {key.casefold() for key in members} or relative not in bound:
            raise ValueError("Unexpected package document path")
        members[name] = root / relative
    members["BUILD-IDENTITY.json"] = identity_path
    members["SBOM.cdx.json"] = sbom_path
    output = run / "clipss2-0.1.3-windows-x64-portable.zip"
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, file in sorted(members.items()):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(info, file.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None or archive.namelist() != sorted(members):
            raise ValueError("Portable ZIP inventory mismatch")
        for info in archive.infolist():
            digest = hashlib.sha256(archive.read(info)).hexdigest().upper()
            if digest != sha(members[info.filename]):
                raise ValueError(f"ZIP entry mismatch: {info.filename}")
            review.append({"path": info.filename, "bytes": info.file_size, "sha256": digest})
    with (run / "portable-verification.json").open("x", encoding="utf-8", newline="\n") as stream:
        json.dump({"name": output.name, "bytes": output.stat().st_size, "sha256": sha(output),
                   "files": review, "releaseApproved": False}, stream, indent=2)
        stream.write("\n")
    print(f"{len(review)} verified ZIP entries; SHA-256 {sha(output)}")


if __name__ == "__main__":
    main()
