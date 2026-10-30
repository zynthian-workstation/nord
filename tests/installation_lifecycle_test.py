"""Nord-owned integration regressions migrated from Manager; no SSH or real device."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "integrations/zynthian"
sys.path.insert(0, str(SOURCE))
import installation as integration  # noqa: E402


class InstallationLifecycle(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nord-install-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.root = self.directory / "zynthian"
        self.ui = self.root / "zynthian-ui/zyngine"
        self.ui.mkdir(parents=True)
        (self.root / "config").mkdir()
        fixtures = ROOT / "tests/fixtures/manager-migration/nord-integration"
        for name, source in [
            ("zynthian_engine_jalv.py", "jalv.txt"),
            ("zynthian_processor.py", "processor.txt"),
        ]:
            shutil.copy2(fixtures / source, self.ui / name)
        self.original = {p: p.read_bytes() for p in self.ui.iterdir()}
        self.job = self.directory / "job"
        self.job.mkdir()
        self.spec = {
            "identity": {
                "repository": "zynthian-workstation/nord",
                "plugin_uri": "urn:nord:instrument",
                "bundle": "Nord.lv2",
            }
        }
        self.bundle = self.directory / "Nord.lv2"
        tools = self.bundle / "tools"
        tools.mkdir(parents=True)
        shutil.copy2(fixtures / "control-layout.tsv", tools / "control-layout.tsv")
        for name in (
            "installation.py",
            "installation_recipe.json",
            "nord_host.py",
            "nord_pages.py",
            "configure_zynthian_nord_ui.py",
            "install_zynthian.py",
        ):
            shutil.copy2(SOURCE / name, tools / name)
        shutil.copy2(ROOT / "workstation.json", self.bundle / "workstation.json")
        self.bundle.joinpath("catalog.nord").write_bytes(
            b"NORD-LIBRARY-2\0piano\0piano\0piano\0category\0category\0npno\0-\0"
            b"sample\0sample\0sample\0category\0category\0nsmp4\0-\0"
            b"pure\0pure\0pure\0category\0category\0pure\0-\0"
        )
        self.bundle.joinpath("manifest.ttl").write_text("""
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
<urn:nord:instrument> a lv2:Plugin ; lv2:binary <test.so> ; rdfs:seeAlso <plugin.ttl> .
""")
        symbols = [
            row.split("\t")[4]
            for row in (tools / "control-layout.tsv").read_text().splitlines()
        ]
        ports = ",\n".join(
            "[ a lv2:InputPort, lv2:ControlPort ; lv2:index "
            + str(i)
            + ' ; lv2:symbol "'
            + s
            + '" ; lv2:name "'
            + s
            + '" ]'
            for i, s in enumerate(symbols)
        )
        self.bundle.joinpath("plugin.ttl").write_text(
            """
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix doap: <http://usefulinc.com/ns/doap#> .
<urn:nord:instrument> a lv2:InstrumentPlugin ; doap:name "Nord" ;
lv2:binary <test.so> ; lv2:port
"""
            + ports
            + " .\n"
        )
        self.bundle.joinpath("test.so").write_bytes(b"\x7fELF" + bytes(60))

    def create(self):
        return integration.NordDynamicPages(self.root, self.job, self.spec)

    def restored(self):
        for path, original in self.original.items():
            self.assertEqual(path.read_bytes(), original)
        for name in ("nord_host.py", "nord_pages.py"):
            self.assertFalse((self.ui / name).exists())

    def test_apply_validate_backup_and_restore_after_process_exit(self):
        instance = self.create()
        instance.validate_bundle(self.bundle)
        state = None

        def checkpoint():
            nonlocal state
            state = json.loads(json.dumps(instance.journal()))

        instance.apply(checkpoint)
        instance.verify()
        self.assertTrue(instance.backup.is_dir())
        restored = integration.NordDynamicPages.from_journal(
            state, self.root, self.spec
        )
        self.assertEqual(restored.rollback(), [])
        self.restored()

    def test_failure_after_every_write_restores_all_originals(self):
        for failed_write in range(1, 5):
            with self.subTest(write=failed_write):
                instance = self.create()
                atomic = instance.setup["atomic"]
                writes = 0

                def fail(path, *args, atomic=atomic, failed_write=failed_write):
                    nonlocal writes
                    atomic(path, *args)
                    writes += 1
                    if writes == failed_write:
                        raise RuntimeError("injected failure")

                instance.setup["atomic"] = fail
                with self.assertRaisesRegex(RuntimeError, "injected"):
                    instance.apply(lambda: None)
                state = instance.journal()
                restored = integration.NordDynamicPages.from_journal(
                    state, self.root, self.spec
                )
                self.assertEqual(restored.rollback(), [])
                self.restored()

    def test_existing_integration_is_idempotent_and_preserved(self):
        original = self.create()
        original.apply(lambda: None)
        snapshot = {p: p.read_bytes() for p in self.ui.iterdir()}
        second = self.create()
        second.apply(lambda: None)
        self.assertEqual(second.changes, {})
        self.assertEqual(second.rollback(), [])
        self.assertEqual(snapshot, {p: p.read_bytes() for p in self.ui.iterdir()})
        self.assertTrue(original.backup.is_dir())

    def test_known_legacy_manager_modules_are_preserved(self):
        fixtures = ROOT / "tests/fixtures/manager-migration/nord-integration"
        for name in ("nord_host", "nord_pages"):
            shutil.copy2(
                fixtures / ("manager-" + name + "_payload.txt"),
                self.ui / (name + ".py"),
            )
        before = {
            name: (self.ui / name).read_bytes()
            for name in ("nord_host.py", "nord_pages.py")
        }
        instance = self.create()
        instance.apply(lambda: None)
        instance.verify()
        for name, original in before.items():
            self.assertEqual((self.ui / name).read_bytes(), original)
        self.assertEqual(instance.rollback(), [])
        for name, original in before.items():
            self.assertEqual((self.ui / name).read_bytes(), original)

    def test_concurrent_edit_is_preserved_with_partial_rollback(self):
        instance = self.create()
        instance.apply(lambda: None)
        path = self.ui / "zynthian_engine_jalv.py"
        path.write_bytes(path.read_bytes() + b"\n# concurrent change\n")
        failures = instance.rollback()
        self.assertTrue(failures)
        self.assertTrue(path.read_bytes().endswith(b"# concurrent change\n"))
        self.assertTrue((self.ui / "nord_host.py").exists())
        self.assertTrue(instance.backup.is_dir())

    def test_unknown_host_and_foreign_module_are_rejected(self):
        path = self.ui / "nord_host.py"
        path.write_text('raise RuntimeError("foreign")')
        with self.assertRaises((RuntimeError, ValueError)):
            self.create()
        path.unlink()
        source = self.ui / "zynthian_processor.py"
        source.write_text(
            source.read_text().replace("return self.ctrl_screens_dict", "return {}")
        )
        with self.assertRaises(ValueError):
            self.create()

    def test_invalid_layout_symbols_indices_and_catalog_rejected(self):
        instance = self.create()
        instance.validate_bundle(self.bundle)
        layout = self.bundle / "tools/control-layout.tsv"
        original = layout.read_text()
        for index, symbol in [(-1, "gain"), (4096, "gain"), (0, "unknown_symbol")]:
            layout.write_text(f"7\t0\tGeneral\t{index}\t{symbol}\t0\t1\t0\n")
            with self.assertRaises(RuntimeError):
                instance.validate_bundle(self.bundle)
        layout.write_text(original)
        (self.bundle / "catalog.nord").write_text("bad catalog")
        with self.assertRaises(RuntimeError):
            instance.validate_bundle(self.bundle)
        self.restored()

    def test_symlink_rejected_without_modifying_destination(self):
        path = self.ui / "nord_host.py"
        target = self.directory / "external"
        target.write_bytes(b"preserve")
        path.symlink_to(target)
        with self.assertRaises((RuntimeError, ValueError)):
            self.create()
        self.assertEqual(target.read_bytes(), b"preserve")

    def test_release_index_matches_packaging_exclusions(self):
        (self.bundle / ".nord-refresh.lock").write_bytes(b"")
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                str(ROOT / "tools/package_release.py"),
                "--bundle",
                str(self.bundle),
                "--architecture",
                "aarch64",
            ],
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        index = json.loads((self.bundle / "workstation-release.json").read_text())
        self.assertNotIn(".nord-refresh.lock", index["files"])
        self.assertEqual(index["executables"]["tools/install_zynthian.py"], "python")
        self.assertEqual(index["version"], "1.0.1")

    def test_cli_pre_post_verify_and_rollback_protocol(self):
        def invoke(stage, action):
            job = self.directory / stage
            job.mkdir(exist_ok=True)
            context = job / "context.json"
            context.write_text(
                json.dumps(
                    dict(
                        schema_version=1,
                        root=str(self.root),
                        package_path=str(self.bundle),
                        bundle_path=str(self.bundle),
                        transaction_directory=str(job),
                        identity=self.spec["identity"],
                        version="1.0.1",
                    )
                )
            )
            result = subprocess.run(
                [
                    sys.executable,
                    "-B",
                    str(self.bundle / "tools/install_zynthian.py"),
                    stage,
                    "--action",
                    action,
                ],
                env=dict(os.environ, WORKSTATION_INSTALL_CONTEXT=str(context)),
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

        for stage in ("pre", "post"):
            for action in ("check", "apply", "verify"):
                invoke(stage, action)
        invoke("pre", "verify")
        invoke("post", "verify")
        invoke("post", "rollback")
        invoke("post", "verify-rollback")
        invoke("pre", "rollback")
        invoke("pre", "verify-rollback")
        self.restored()


if __name__ == "__main__":
    unittest.main()
