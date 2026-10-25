#!/usr/bin/env python3
"""Package tracked + nonignored source edits, never build outputs or credentials."""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def create(output):
    manifest = {"created": datetime.now(timezone.utc).isoformat(), "toolchain": "C++20 / Swift 6.0.3 / Ubuntu 22.04 ARM64",
                "repositories": {}, "files": {}}
    files = []
    for repo in (ROOT, ROOT.parent / "deskpet-pipeline"):
        head = subprocess.check_output(["git", "-C", repo, "rev-parse", "HEAD"], text=True).strip()
        names = subprocess.check_output(["git", "-C", repo, "ls-files", "--cached", "--others", "--exclude-standard", "-z"]).decode("utf-8").split("\0")
        manifest["repositories"][repo.name] = {"head": head, "includesWorkingTreeChanges": bool(subprocess.check_output(["git", "-C", repo, "status", "--porcelain"]))}
        for name in sorted(set(filter(None, names))):
            path = repo / name
            if not path.is_file():
                continue
            if path.is_symlink() or any(part in (".git", ".build", ".swiftpm", "__pycache__") for part in path.parts):
                continue
            data = path.read_bytes()
            archive_name = repo.name + "/" + name
            manifest["files"][archive_name] = hashlib.sha256(data).hexdigest()
            files.append((archive_name, data))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w:gz") as archive:
        files.append(("manifest.json", (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode()))
        sums = "".join(f"{digest}  {name}\n" for name, digest in manifest["files"].items())
        files.append(("SHA256SUMS", sums.encode()))
        for name, data in files:
            info = tarfile.TarInfo("deskpet-board/" + name)
            info.size = len(data); info.mode = 0o755 if name.endswith(".sh") else 0o644
            archive.addfile(info, io.BytesIO(data))
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(output.suffix + ".sha256").write_text(f"{digest}  {output.name}\n")
    print(f"{output}: {len(manifest['files'])} source files, sha256={digest}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    create(parser.parse_args().output.resolve())
