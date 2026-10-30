#!/usr/bin/env python3
"""Generate the package-owned integrity/executable inventory after staging all files."""

import argparse
import fnmatch
import hashlib
import json
from pathlib import Path


def generate(bundle, architecture):
    architecture = {"arm64": "aarch64", "amd64": "x86_64"}.get(
        architecture, architecture
    )
    manifest = json.loads((bundle / "workstation.json").read_text())
    files, executables = {}, {}
    for path in sorted(bundle.rglob("*")):
        if path.is_symlink():
            raise ValueError("Symlink in release bundle")
        if (
            not path.is_file()
            or path.name == "workstation-release.json"
            or any(
                fnmatch.fnmatch(path.name, pattern)
                for pattern in ("*.tmp*", "*.stamp", ".nord-refresh.lock")
            )
        ):
            continue
        name = path.relative_to(bundle).as_posix()
        with path.open("rb") as stream:
            files[name] = hashlib.file_digest(stream, "sha256").hexdigest()
        if path.suffix == ".py":
            executables[name] = "python"
        elif name == "tools/nord_catalog":
            executables[name] = "native"
    data = dict(
        schema_version=1,
        repository=manifest["repository"],
        version=manifest["version"],
        plugin_uri=manifest["plugin"]["uri"],
        bundle=manifest["plugin"]["bundle"],
        os="linux",
        architecture=architecture,
        files=files,
        executables=executables,
    )
    (bundle / "workstation-release.json").write_text(json.dumps(data, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--architecture", required=True)
    args = parser.parse_args()
    generate(args.bundle, args.architecture)
