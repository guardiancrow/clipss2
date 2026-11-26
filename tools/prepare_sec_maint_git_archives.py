# Copyright (c) 2026 guardiancrow. MIT.
"""Regenerate the pinned AOM and libyuv vcpkg downloads in a fresh directory."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from verify_sec_maint_3e_git_archive import git_files, verify_archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    plan = json.loads((root / "cmake/security/sec-maint-v013/3f/source-inputs-v1.json").read_bytes())
    run = args.run.resolve(strict=True)
    downloads = run / "downloads"
    downloads.mkdir(exist_ok=True)
    area = run / "git-source-verification"
    area.mkdir()
    env = os.environ.copy()
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_OPTIONAL_LOCKS="0", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")

    def git(*argv):
        return subprocess.run(["git", "-c", "core.autocrlf=false", *map(str, argv)],
                              env=env, stdout=subprocess.PIPE, check=True).stdout

    version = git("--version").decode("ascii").strip()
    if version != plan["gitAcquisition"]["version"]:
        raise SystemExit(f"Expected {plan['gitAcquisition']['version']}, got {version}")

    for name in ("aom", "libyuv"):
        spec = plan[name]
        repo = area / name
        git("init", "--bare", repo)
        fetch_ref = "refs/tags/" + spec["tag"] if "tag" in spec else spec["commit"]
        git("-C", repo, "fetch", "--no-tags", "--depth=1", spec["source"], fetch_ref)
        git("-C", repo, "fsck", "--strict", "--full")
        files, attributes = git_files(repo, spec["commit"], spec["tree"])
        archive = area / spec["archive"]
        git("-C", repo, "archive", spec["commit"], "-o", archive)
        if archive.stat().st_size != spec["archiveBytes"]:
            raise ValueError(f"{name}: regenerated archive size differs")
        with archive.open("rb") as stream:
            verified = verify_archive(stream, spec["archiveSha512"], spec["commit"], files)
        record = {"commit": spec["commit"], "tree": spec["tree"], "archive": archive.name,
                  "sha512": spec["archiveSha512"], "bytes": archive.stat().st_size,
                  "fileCount": len(verified), "attributes": attributes, "files": verified}
        data = (json.dumps(record, sort_keys=True, indent=2) + "\n").encode()
        if len(verified) != spec["archiveFiles"] or hashlib.sha256(data).hexdigest().upper() != spec["archiveFileLedgerSha256"]:
            raise ValueError(f"{name}: regenerated archive file ledger differs")
        (area / f"{name}-archive-files.json").write_bytes(data)
        destination = downloads / archive.name
        if destination.exists():
            with destination.open("rb") as stream:
                actual = hashlib.file_digest(stream, "sha512").hexdigest().upper()
            if actual != spec["archiveSha512"]:
                raise ValueError(f"{name}: existing download differs")
        else:
            with archive.open("rb") as source, destination.open("xb") as target:
                shutil.copyfileobj(source, target)
        print(f"{name}: {len(verified)} Git files verified; SHA-512 matches", flush=True)


if __name__ == "__main__":
    main()
