#!/usr/bin/env python3
"""Attribute deployed ELF libraries to the installed Qt and Debian inputs."""
import argparse
import functools
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

DOC = Path("usr/share/doc/automotive-format-explorer")
APP = Path("usr/bin/automotive-format-explorer")


def run(*args):
    return subprocess.check_output(args, text=True, env={**os.environ, "LC_ALL": "C"})


def digest(path, algorithm="sha256"):
    return hashlib.new(algorithm, path.read_bytes()).hexdigest()


@functools.lru_cache(maxsize=None)
def identity(path):
    text = run("readelf", "-W", "-h", "-n", "-d", str(path))
    build = re.search(r"Build ID: (\w+)", text)
    if not build:
        raise ValueError(f"{path}: ELF has no GNU build-id")
    dynamic = tuple(sorted(re.findall(r"\((NEEDED|SONAME)\).*?\[(.*?)\]", text)))
    return build[1], dynamic


def is_elf(path):
    if not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def payloads(appdir):
    # Inspect the complete tree, including native QML modules and Qt plugins.
    executable = (appdir / APP).resolve()
    return sorted(p for p in appdir.rglob("*")
                  if p.resolve() != executable and is_elf(p))


def system_libraries():
    return sorted({Path(line.split(" => ", 1)[1].strip())
                   for line in run("ldconfig", "-p").splitlines() if " => " in line})


def debian_package(source):
    # usrmerge leaves some dpkg file lists under /lib, others under /usr/lib.
    paths = [source, source.resolve()]
    resolved = str(source.resolve())
    if resolved.startswith("/usr/"):
        paths.append(Path(resolved[4:]))
    for path in dict.fromkeys(paths):
        result = subprocess.run(["dpkg-query", "-S", str(path)], text=True, capture_output=True)
        if result.returncode == 0:
            owners = {line.rsplit(": ", 1)[0] for line in result.stdout.splitlines()
                      if line.endswith(": " + str(path))}
            if len(owners) == 1:
                owner = owners.pop()
                version = run("dpkg-query", "-W", "-f=${Version}", owner)
                return owner, version, Path("/usr/share/doc") / owner.split(":")[0] / "copyright"
    raise ValueError(f"{source}: no installed Debian package owner")


def generate(appdir, qt, material):
    identity.cache_clear()
    doc = appdir / DOC
    licenses = doc / "licenses"
    if licenses.exists():
        shutil.rmtree(licenses)
    licenses.mkdir(parents=True, exist_ok=True)
    copied = {}

    def copy_text(source):
        if not source.is_file() or not source.read_bytes().strip():
            raise ValueError(f"missing license material {source}")
        checksum = digest(source)
        if checksum not in copied:
            target = licenses / source.name
            if target.exists() and digest(target) != checksum:
                target = licenses / (checksum[:12] + "-" + source.name)
            shutil.copyfile(source, target)
            copied[checksum] = target.relative_to(doc).as_posix()
        return copied[checksum]

    sboms = {}
    qt_files = {}
    for path in sorted((qt / "sbom").glob("*.spdx.json")):
        data = json.loads(path.read_text())
        sboms[path] = data
        for record in data.get("files", []):
            original = (qt / record["fileName"]).resolve()
            qt_files.setdefault(original, []).append((path, record))

    # Names narrow the search; build-id and dynamic dependencies establish origin.
    candidates = {}
    for origin, paths in (("Qt", qt.rglob("*.so*")), ("Debian", system_libraries())):
        for path in sorted(paths):
            if path.is_file():
                candidates.setdefault(path.name, set()).add((origin, path.resolve()))

    common = {"GPL-2.0-only": "GPL-2", "GPL-2.0-or-later": "GPL-2",
              "GPL-3.0-only": "GPL-3", "LGPL-2.1-or-later": "LGPL-2.1",
              "LGPL-3.0-only": "LGPL-3"}

    def qt_material(path, data):
        refs = {copy_text(path)}
        extracted = {r["licenseId"]: r.get("extractedText", "")
                     for r in data.get("hasExtractedLicensingInfos", [])}
        expressions = [r.get("licenseConcluded", "NOASSERTION")
                       for r in data.get("packages", []) + data.get("files", [])]
        for name in sorted(set(re.findall(r"[A-Za-z0-9][A-Za-z0-9.+-]*", " ".join(expressions)))):
            if name in {"AND", "OR", "WITH", "NOASSERTION", "NONE", "LicenseRef-Qt-Commercial"}:
                continue
            if name in extracted:
                if not extracted[name].strip():
                    raise ValueError(f"empty extracted license {name} in {path}")
                continue  # Full text is already in the copied SPDX document.
            source = {"GPL-3.0-only": material.parent.parent / "LICENSE",
                      "LGPL-3.0-only": material / "LGPL-3.0.txt"}.get(name, material / (name + ".txt"))
            if not source.is_file():
                source = Path("/usr/share/common-licenses") / common.get(name, name)
            if not source.is_file():
                raise ValueError(f"missing license text {name} for {path}")
            refs.add(copy_text(source))
        return refs

    module_refs = {}
    entries = []
    for payload in payloads(appdir):
        rel = payload.relative_to(appdir).as_posix()
        try:
            key = identity(payload)
            choices = candidates.get(payload.name, set()) | candidates.get(payload.resolve().name, set())
            matches = [(origin, source) for origin, source in sorted(choices) if identity(source) == key]
            if len(matches) != 1:
                raise ValueError(f"unknown or ambiguous origin ({len(matches)} matches)")
            origin, source = matches[0]
            refs = set()
            if origin == "Qt":
                records = qt_files.get(source, [])
                if records:
                    labels = []
                    for sbom, record in records:
                        for check in record.get("checksums", []):
                            algorithm = check["algorithm"].lower().replace("-", "")
                            if digest(source, algorithm) != check["checksumValue"].lower():
                                raise ValueError(f"Qt SBOM checksum differs for {source}")
                        expression = record.get("licenseConcluded", "NOASSERTION")
                        if expression in {"NOASSERTION", "NONE"}:
                            raise ValueError(f"Qt SBOM has no license for {source}")
                        labels.append(expression)
                        if sbom not in module_refs:
                            module_refs[sbom] = qt_material(sbom, sboms[sbom])
                        refs.update(module_refs[sbom])
                    label = "Qt: " + "; ".join(sorted(set(labels)))
                elif re.fullmatch(r"libicu\w+\.so\.\d+\.\d+", source.name):
                    versions = {p.get("versionInfo") for d in sboms.values() for p in d.get("packages", [])
                                if p.get("name") == "ICU"}
                    version = source.name.split(".so.")[1]
                    if versions != {version}:
                        raise ValueError(f"ICU {version} does not match installed Qt SBOM versions {sorted(versions)}")
                    manifest = json.loads((material / "linux-materials.json").read_text())
                    notice = manifest.get("ICU", {}).get(version)
                    if not notice:
                        raise ValueError(f"missing ICU {version} license provenance")
                    license_file = material / notice["file"]
                    if digest(license_file) != notice["sha256"]:
                        raise ValueError(f"ICU {version} license checksum differs")
                    refs.add(copy_text(license_file))
                    label = f"ICU {version} supplied by Qt; upstream notice: {notice['source']}"
                else:
                    raise ValueError(f"no Qt SBOM file record for {source}")
            else:
                package, version, copyright_file = debian_package(source)
                refs.add(copy_text(copyright_file))
                for name in sorted(set(re.findall(r"/usr/share/common-licenses/([A-Za-z0-9.+-]+)",
                                                   copyright_file.read_text()))):
                    refs.add(copy_text(Path("/usr/share/common-licenses") / name.rstrip(".")))
                label = f"Debian package {package} {version}"
            entries.append({"path": rel, "origin": label, "source": str(source),
                            "build_id": key[0], "sha256": digest(payload), "licenses": sorted(refs)})
        except (ValueError, OSError, subprocess.CalledProcessError) as error:
            raise ValueError(f"{rel}: {error}") from error
    if not entries:
        raise ValueError("AppDir contains no third-party ELF payloads")
    lines = ["Automotive Format Explorer: GNU GPL v3 (LICENSE).", "",
             "Bundled third-party ELF files, attributed to installed build inputs.",
             "Qt SPDX documents retain module and embedded dependency notices and license expressions.",
             "Qt is distributed under its open-source license alternatives; commercial licenses are not selected.",
             "RPATH changes are allowed; origin matches GNU build-id, SONAME and NEEDED entries.", ""]
    for entry in entries:
        lines.extend([entry["path"], "  " + entry["origin"], "  Original: " + entry["source"],
                      "  GNU build-id: " + entry["build_id"], "  Material: " + ", ".join(entry["licenses"]), ""])
    (doc / "THIRD_PARTY_NOTICES.txt").write_text("\n".join(lines))
    (doc / "third-party-files.json").write_text(json.dumps(entries, indent=2, sort_keys=True) + "\n")
    verify(appdir)


def verify(appdir):
    doc = appdir / DOC
    entries = json.loads((doc / "third-party-files.json").read_text())
    actual = {p.relative_to(appdir).as_posix(): digest(p) for p in payloads(appdir)}
    expected = {e["path"]: e["sha256"] for e in entries}
    if actual != expected:
        raise ValueError("third-party ELF coverage/content changed after notices generation")
    for path in {r for e in entries for r in e["licenses"]} | {"THIRD_PARTY_NOTICES.txt"}:
        if not (doc / path).is_file() or not (doc / path).read_bytes().strip():
            raise ValueError(f"missing or empty notice material: {path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appdir", type=Path)
    parser.add_argument("--qt-prefix", type=Path, default=os.environ.get("QT_PREFIX"))
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    try:
        if args.verify:
            verify(args.appdir)
        else:
            if not args.qt_prefix:
                parser.error("--qt-prefix or QT_PREFIX is required")
            generate(args.appdir, Path(args.qt_prefix), Path(__file__).resolve().parents[1] / "resources/licenses")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
