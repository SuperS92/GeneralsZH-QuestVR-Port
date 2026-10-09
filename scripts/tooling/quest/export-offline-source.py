#!/usr/bin/env python3
"""Export the only authorized Quest donor revision; never check out the donor.

Usage: python export-offline-source.py --donor PATH --output work/offline-v044
The output is local evidence, never an engine foundation or a public asset pack.
"""
import argparse
import hashlib
import json
import stat
import subprocess
import zipfile
from pathlib import Path

OFFLINE_COMMIT = "176a7ce77b615fb8681f8c250625fd3fb5acb4b9"
APK_SHA256 = "6d83a65ed0e3165207694db7c1cb92d8a137968719a2564a5781e59cad7c82cc"


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    donor, output = args.donor.resolve(), args.output.resolve()
    if output == donor or donor in output.parents:
        parser.error("the snapshot must be outside the read-only donor")
    evidence = output.parent / "evidence"
    evidence.mkdir(parents=True, exist_ok=True)
    archive = evidence / "offline-v044.zip"
    command = ["git", "--no-optional-locks", "--git-dir=" + str(donor / "work/project-git"),
               "--work-tree=" + str(donor)]
    actual = subprocess.check_output(command + ["rev-parse", OFFLINE_COMMIT + "^{commit}"], text=True).strip()
    if actual != OFFLINE_COMMIT:
        raise RuntimeError("unexpected donor object identity")
    if not archive.exists():
        subprocess.run(command + ["archive", "--format=zip", "--output=" + str(archive), OFFLINE_COMMIT], check=True)
    with zipfile.ZipFile(archive) as source:
        if source.comment.decode("ascii") != OFFLINE_COMMIT:
            raise RuntimeError("archive does not identify the authorized commit")
        entries = source.infolist()
        for entry in entries:
            target = output / entry.filename
            if output not in target.resolve().parents:
                raise RuntimeError("unsafe archive member: " + entry.filename)
            if entry.is_dir():
                continue
            expected = source.read(entry)
            if target.exists():
                if target.read_bytes() != expected:
                    raise RuntimeError("existing snapshot differs: " + entry.filename)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(expected)
            target.chmod(stat.S_IREAD)
    files = {entry.filename: sha256(output / entry.filename) for entry in entries if not entry.is_dir()}
    apk = donor / "outputs/archive/GeneralsVRQuest3-0.44-quest-alpha-debug.apk"
    if sha256(apk) != APK_SHA256:
        raise RuntimeError("offline reference APK does not match the handoff")
    manifest = {"schema": 1, "commit": actual, "archive_sha256": sha256(archive),
                "apk_sha256": APK_SHA256, "read_only": True,
                "dxvk_source_marker": (output / "references/fbraz3-dxvk/.generalsvrquest3-source").read_text().strip(),
                "files": files}
    (evidence / "offline-v044-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(files)} read-only files from {actual}; APK identity matches.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
