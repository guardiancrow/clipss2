# Copyright (c) 2026 guardiancrow. MIT.
"""Verify the stock vcpkg_from_git archive against a fixed local Git object."""

import argparse
import gzip
import hashlib
import io
import json
import ntpath
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import unicodedata


def git(repo, *args, data=None):
    env = os.environ.copy()
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_NO_REPLACE_OBJECTS="1", GIT_OPTIONAL_LOCKS="0")
    return subprocess.run(
        ["git", "-c", f"safe.directory={repo.as_posix()}", "-C", str(repo), *args],
        env=env, input=data, stdout=subprocess.PIPE, check=True,
    ).stdout


def safe_name(name):
    parts = name.split("/")
    if (any(part in ("", ".", "..") or ntpath.isreserved(part) for part in parts)
            or name != unicodedata.normalize("NFC", name)):
        raise ValueError("unsafe archive path")
    return name


def git_files(repo, commit, tree):
    if git(repo, "rev-parse", "FETCH_HEAD^{commit}").strip().decode() != commit:
        raise ValueError("fixed commit mismatch")
    if git(repo, "rev-parse", f"{commit}^{{tree}}").strip().decode() != tree:
        raise ValueError("fixed tree mismatch")
    entries = {}
    for item in git(repo, "ls-tree", "-rz", commit).split(b"\0")[:-1]:
        record, name = item.split(b"\t", 1)
        mode, kind, oid = record.decode("ascii").split()
        key = safe_name(name.decode("utf-8"))
        if kind != "blob" or mode not in ("100644", "100755"):
            raise ValueError("unexpected submodule or symbolic link")
        if PurePosixPath(key).name in (".gitmodules", ".lfsconfig"):
            raise ValueError("unexpected external Git input")
        entries[key] = (mode, oid)
    by_oid = set(oid for _, oid in entries.values())
    objects = io.BytesIO(git(repo, "cat-file", "--batch",
                            data=("\n".join(sorted(by_oid)) + "\n").encode()))
    digests = {}
    for wanted in sorted(by_oid):
        oid, kind, length = objects.readline().decode("ascii").split()
        blob = objects.read(int(length))
        if (oid, kind) != (wanted, "blob") or len(blob) != int(length) or objects.read(1) != b"\n":
            raise ValueError("invalid Git object response")
        if blob.startswith(b"version https://git-lfs.github.com/spec/v1\n"):
            raise ValueError("unexpected Git LFS pointer")
        digests[oid] = hashlib.sha256(blob).hexdigest()
    attributes = [name for name in entries if PurePosixPath(name).name == ".gitattributes"]
    for name in attributes:
        data = git(repo, "cat-file", "blob", entries[name][1])
        for line in data.splitlines():
            if not line.lstrip().startswith(b"#") and re.search(rb"(?<!\S)(?:export-ignore|export-subst|filter=lfs)(?!\S)", line):
                raise ValueError("unexpected Git archive or LFS attribute")
    return {name: (mode, digests[oid]) for name, (mode, oid) in entries.items()}, attributes


def verify_archive(stream, archive_sha512, commit, files):
    if hashlib.file_digest(stream, "sha512").hexdigest() != archive_sha512.lower():
        raise ValueError("archive SHA-512 mismatch")
    stream.seek(0)
    directories = {
        parent.as_posix()
        for name in files for parent in PurePosixPath(name).parents
        if parent.as_posix() != "."
    }
    seen = set()
    confirmed = {}
    with gzip.GzipFile(fileobj=stream, mode="rb") as data, tarfile.open(fileobj=data, mode="r:") as archive:
        for item in archive:
            name = safe_name(item.name)
            folded = name.casefold()
            if folded in seen:
                raise ValueError("duplicate archive entry")
            seen.add(folded)
            if item.isdir() and name in directories:
                continue
            if item.type not in (tarfile.REGTYPE, tarfile.AREGTYPE) or item.sparse:
                raise ValueError("unsupported archive entry")
            if name not in files:
                raise ValueError("unexpected archive file")
            mode, want = files[name]
            git_mode = "100755" if item.mode & 0o111 else "100644"
            with archive.extractfile(item) as content:
                digest = hashlib.file_digest(content, "sha256").hexdigest()
            if git_mode != mode or item.mode & ~0o777 or digest != want:
                raise ValueError("archive file differs from Git")
            confirmed[name] = {"mode": mode, "sha256": digest}
        if archive.pax_headers.get("comment") != commit:
            raise ValueError("archive commit mismatch")
        data.seek(archive.offset)
        padding = data.read()
        if len(padding) < 1024 or any(padding):
            raise ValueError("unexpected data after tar entries")
    if files.keys() != confirmed.keys():
        raise ValueError("missing archive file")
    if not {name.casefold() for name in directories} <= seen:
        raise ValueError("missing archive directory")
    return confirmed


def check_negative_cases():
    commit = "1" * 40
    mode = "100644"
    content = b"approved synthetic content"
    approved = {"sample.txt": (mode, hashlib.sha256(content).hexdigest())}

    def stream(entries, archive_commit=commit):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:gz", format=tarfile.PAX_FORMAT,
                          pax_headers={"comment": archive_commit}) as archive:
            for name, data, entry_type in entries:
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.type = entry_type
                archive.addfile(info, io.BytesIO(data))
        buffer.seek(0)
        return buffer

    regular = tarfile.REGTYPE
    original = [("sample.txt", content, regular)]
    reference = stream(original)
    fixed = hashlib.sha512(reference.getvalue()).hexdigest()
    verify_archive(reference, fixed, commit, approved)
    cases = (
        ("wrong commit", stream(original, "2" * 40), approved, "archive commit mismatch"),
        ("wrong tree", stream(original), {"sample.txt": (mode, "0" * 64)}, "archive file differs from Git"),
        ("changed file", stream([("sample.txt", b"changed", regular)]), approved, "archive file differs from Git"),
        ("missing file", stream(original), approved | {"missing.txt": approved["sample.txt"]}, "missing archive file"),
        ("extra file", stream(original + [("extra.txt", b"", regular)]), approved, "unexpected archive file"),
        ("same cache name, changed bytes", stream([("sample.txt", b"changed", regular)]), approved, "archive SHA-512 mismatch"),
        ("unsafe path", stream(original + [("../outside.txt", b"", regular)]), approved, "unsafe archive path"),
        ("duplicate entry", stream(original + original), approved, "duplicate archive entry"),
        ("special file", stream(original + [("pipe", b"", tarfile.FIFOTYPE)]), approved, "unsupported archive entry"),
    )
    rejected = []
    for name, archive, tree_files, expected in cases:
        digest = fixed if name.startswith("same cache name") else hashlib.sha512(archive.getvalue()).hexdigest()
        try:
            verify_archive(archive, digest, commit, tree_files)
        except ValueError as error:
            if str(error) != expected:
                raise
            rejected.append(name)
        else:
            raise AssertionError(f"archive accepted: {name}")
    return rejected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--tree")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--sha512")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--test", action="store_true")
    args = parser.parse_args()
    if args.test:
        result = {"rejected": check_negative_cases()}
    else:
        if not all((args.repo, args.commit, args.tree, args.archive, args.sha512)):
            parser.error("archive verification requires a repository, commit, tree, archive and SHA-512")
        files, attributes = git_files(args.repo.resolve(), args.commit, args.tree)
        with args.archive.open("rb") as stream:
            verified = verify_archive(stream, args.sha512, args.commit, files)
        result = {
            "commit": args.commit, "tree": args.tree, "archive": args.archive.name,
            "sha512": args.sha512, "bytes": args.archive.stat().st_size,
            "fileCount": len(verified), "attributes": attributes, "files": verified,
        }
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(result, output, sort_keys=True, indent=2)
        output.write("\n")


if __name__ == "__main__":
    main()
