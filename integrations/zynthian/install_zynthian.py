#!/usr/bin/env python3
"""Nord lifecycle protocol v1; never SSH, restart or register other engines."""

import argparse
import json
import os
import sys
from pathlib import Path

from installation import NordDynamicPages, installation_path, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=("pre", "post"))
    parser.add_argument(
        "--action",
        required=True,
        choices=("check", "apply", "verify", "rollback", "verify-rollback"),
    )
    args = parser.parse_args()
    context_path = Path(os.environ["WORKSTATION_INSTALL_CONTEXT"])
    context = json.loads(context_path.read_text())
    require(context["schema_version"] == 1, "Unsupported lifecycle protocol")
    identity = context["identity"]
    require(
        identity["repository"] == "zynthian-workstation/nord"
        and identity["plugin_uri"] == "urn:nord:instrument"
        and identity["bundle"] == "Nord.lv2",
        "Foreign installation identity",
    )
    root = Path(context["root"])
    package = Path(context["package_path"])
    require(
        json.loads((package / "workstation.json").read_text())["version"]
        == context["version"],
        "Package version mismatch",
    )
    job = context_path.parent
    require(
        job == Path(context["transaction_directory"]), "Invalid transaction context"
    )
    state_path = job / "nord-integration.json"
    spec = dict(identity=identity)
    if args.stage == "pre":
        if args.action not in ("rollback", "verify-rollback"):
            integration = NordDynamicPages(root, job, spec)
            integration.validate_bundle(package)
        return
    if args.action in ("rollback", "verify-rollback"):
        if not state_path.exists():
            return
        integration = NordDynamicPages.from_journal(
            json.loads(state_path.read_text()), root, spec
        )
        if args.action == "rollback":
            failures = integration.rollback()
            require(not failures, "Partial Nord rollback: " + "; ".join(failures))
        else:
            for path, original in integration.originals.items():
                installation_path(path, root)
                require(
                    (path.read_bytes() if path.exists() else None) == original,
                    "Nord integration not restored: " + str(path),
                )
        return
    if args.action == "verify":
        if state_path.exists():
            integration = NordDynamicPages.from_journal(
                json.loads(state_path.read_text()), root, spec
            )
        else:
            integration = NordDynamicPages(root, job, spec)
        integration.verify()
        integration.validate_bundle(Path(context["bundle_path"]))
        return
    integration = NordDynamicPages(root, job, spec)
    integration.validate_bundle(package)
    if args.action == "apply":

        def checkpoint():
            temporary = state_path.with_suffix(".tmp")
            with temporary.open("w") as stream:
                json.dump(integration.journal(), stream)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, state_path)

        checkpoint()
        integration.apply(checkpoint)
        checkpoint()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
