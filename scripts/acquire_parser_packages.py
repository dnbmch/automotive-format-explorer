#!/usr/bin/env python3
"""Install SHA256-pinned parser archives before configuring CMake (Python 3.12+)."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.request

PARSERS = {"a2lparser", "dbcparser", "ldfparser", "mdf4parser"}


def acquire(lock_path, platform, prefix):
    packages = json.loads(Path(lock_path).read_text(encoding="utf-8"))[platform]
    if len(packages) != len(PARSERS) or {p["name"] for p in packages} != PARSERS:
        raise ValueError("The platform lock must contain exactly the four parser packages")
    prefix = Path(prefix).resolve()
    if prefix.exists():
        raise ValueError(f"Use a new install prefix; destination already exists: {prefix}")
    prefix.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=prefix.parent) as temporary:
        staging = Path(temporary) / "prefix"
        staging.mkdir()
        for package in packages:
            name, url, expected = package["name"], package["url"], package["sha256"]
            if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
                raise ValueError(f"{name}: sha256 must be 64 lowercase hex digits")
            if not url.startswith(("https://", "file://")):
                raise ValueError(f"{name}: use an HTTPS URL or a local file URI")
            archive = Path(temporary) / f"{name}.tar.gz"
            digest = hashlib.sha256()
            with urllib.request.urlopen(url, timeout=60) as source, archive.open("wb") as dest:
                while chunk := source.read(1024 * 1024):
                    digest.update(chunk)
                    dest.write(chunk)
            if digest.hexdigest() != expected:
                raise ValueError(f"{name}: SHA256 mismatch")
            extracted = Path(temporary) / name
            with tarfile.open(archive) as tar:
                tar.extractall(extracted, filter="data")
            if not (extracted / "share" / name / "build-info.json").is_file():
                raise ValueError(f"{name}: missing producer build identity")
            if not list(extracted.glob(f"lib*/cmake/{name}/{name}Config.cmake")):
                raise ValueError(f"{name}: missing installed CMake package")
            # Format-scoped installs may share directories, never files.
            for path in extracted.rglob("*"):
                if path.is_file() and (staging / path.relative_to(extracted)).exists():
                    raise ValueError(f"{name}: overlapping package file {path.relative_to(extracted)}")
            shutil.copytree(extracted, staging, dirs_exist_ok=True)
        shutil.copyfile(lock_path, staging / "parser-package-lock.json")
        staging.rename(prefix)
    return prefix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lock", help="JSON: platform -> list of {name, url, sha256}")
    parser.add_argument("platform", help="Explicit producer platform, e.g. x86_64-windows-mingw")
    parser.add_argument("prefix", help="New directory for the complete installed packages")
    args = parser.parse_args()
    print(acquire(args.lock, args.platform, args.prefix))


if __name__ == "__main__":
    main()
