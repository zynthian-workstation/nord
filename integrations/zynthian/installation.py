"""Nord-owned integration transaction, invoked by the package lifecycle entry point."""

import ast
import base64
import ctypes as C
import ctypes.util
import hashlib
import json
import os
import shutil
import stat
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
URI = "urn:nord:instrument"
LIMIT = 4 * 1024 * 1024
NORD_UI_SETUP_SOURCE = (HERE / "configure_zynthian_nord_ui.py").read_text()
NORD_UI_ASSETS = {
    name: (HERE / name).read_bytes() for name in ("nord_host.py", "nord_pages.py")
}
NORD_RECIPE = json.loads((HERE / "installation_recipe.json").read_text())


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def installation_path(path, root):
    path = Path(path)
    require(
        path.is_absolute() and root in path.parents and ".." not in path.parts,
        "Destination outside Zynthian",
    )
    require(
        not any(p.is_symlink() for p in (path, *path.parents)), "Unexpected symlink"
    )
    return path


safe_path = installation_path


def digest_tree(path):
    return {
        p.relative_to(path).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(path.rglob("*"))
        if p.is_file()
    }


class MetadataWorld:
    def __init__(self, bundle):
        self.lib = C.CDLL(ctypes.util.find_library("lilv-0") or "liblilv-0.so.0")
        self.P = C.c_void_p
        self.world = self.api("lilv_world_new", self.P)()
        node = self.api("lilv_new_uri", self.P, self.P, C.c_char_p)(
            self.world, (Path(bundle).resolve().as_uri() + "/").encode()
        )
        self.api("lilv_world_load_bundle", None, self.P, self.P)(self.world, node)
        self.api("lilv_node_free", None, self.P)(node)
        plugins = self.api("lilv_world_get_all_plugins", self.P, self.P)(self.world)
        node = self.api("lilv_new_uri", self.P, self.P, C.c_char_p)(
            self.world, URI.encode()
        )
        self.plugin = self.api("lilv_plugins_get_by_uri", self.P, self.P, self.P)(
            plugins, node
        )
        self.api("lilv_node_free", None, self.P)(node)
        if not self.plugin:
            self.close()
            raise ValueError("Nord URI not found in bundle")

    def api(self, name, result, *args):
        function = getattr(self.lib, name)
        function.restype, function.argtypes = result, list(args)
        return function

    def string(self, node):
        return self.api("lilv_node_as_string", C.c_char_p, self.P)(node).decode()

    def close(self):
        self.api("lilv_world_free", None, self.P)(self.world)


class NordDynamicPages:
    def __init__(self, root, job, spec):
        self.root, self.spec = root, spec
        self.setup = {"__name__": "nord_ui_setup"}
        exec(compile(NORD_UI_SETUP_SOURCE, "nord_ui_setup", "exec"), self.setup)
        self.backup = None
        self.backup_snapshot = None
        self.created_dirs = []
        self.attempted = []
        assets = Path(job) / "manager-ui-assets"
        assets.mkdir(exist_ok=True)
        for name, content in NORD_UI_ASSETS.items():
            (assets / name).write_bytes(content)
        self.directory = installation_path(root / "zynthian-ui/zyngine", root)
        self.originals = {}
        self.expected = {}
        self.modes = {}
        for name in (
            "zynthian_engine_jalv.py",
            "zynthian_processor.py",
            *NORD_UI_ASSETS,
        ):
            path = installation_path(self.directory / name, root)
            require(
                not path.exists() or path.is_file(), "Destino de integração inválido"
            )
            old = path.read_bytes() if path.exists() else None
            self.originals[path] = old
            self.modes[path] = (
                stat.S_IMODE(path.stat().st_mode) if old is not None else 0o644
            )
        # Reuse fingerprint checking, hook placement and idempotency from Nord.
        self.changes = self.setup["plan"](root / "zynthian-ui", assets)
        for path, old in self.originals.items():
            if path.name in NORD_UI_ASSETS:
                expected = NORD_UI_ASSETS[path.name]
                require(
                    old is None
                    or old == expected
                    or self.setup["fingerprint"](ast.parse(old))
                    in (
                        NORD_RECIPE["modules"][path.name],
                        NORD_RECIPE.get("legacy_manager_modules", {}).get(path.name),
                        self.setup["fingerprint"](ast.parse(expected)),
                    ),
                    "Módulo Nord preexistente diferente; não será sobrescrito: "
                    + str(path),
                )
            else:
                expected = self.setup["patch_text"](old.decode(), path.stem).encode()
            if path.name in NORD_UI_ASSETS and old is not None:
                expected = old
                self.changes.pop(path, None)
            self.expected[path] = expected
        self.bundle = None

    def journal(self):
        def encode(value):
            return base64.b64encode(value).decode() if value is not None else None

        return dict(
            backup=str(self.backup) if self.backup else None,
            backup_snapshot=self.backup_snapshot,
            created_dirs=[str(p) for p in self.created_dirs],
            files=[
                dict(
                    path=str(p),
                    old=encode(old),
                    expected=encode(self.expected[p]),
                    mode=self.modes[p],
                    changed=p in self.changes,
                )
                for p, old in self.originals.items()
            ],
        )

    @classmethod
    def from_journal(cls, state, root, spec):
        self = cls.__new__(cls)
        self.root, self.spec = root, spec
        self.directory = installation_path(root / "zynthian-ui/zyngine", root)
        self.setup = {"__name__": "nord_ui_setup"}
        exec(compile(NORD_UI_SETUP_SOURCE, "nord_ui_setup", "exec"), self.setup)
        self.originals, self.expected, self.modes, self.changes = {}, {}, {}, {}
        allowed = {"zynthian_engine_jalv.py", "zynthian_processor.py", *NORD_UI_ASSETS}
        require(len(state["files"]) == 4, "Invalid integration journal")
        for entry in state["files"]:
            path = installation_path(Path(entry["path"]), root)
            require(
                path.parent == self.directory and path.name in allowed,
                "Foreign file in integration journal",
            )
            allowed.remove(path.name)
            old = (
                base64.b64decode(entry["old"], validate=True)
                if entry["old"] is not None
                else None
            )
            new = base64.b64decode(entry["expected"], validate=True)
            self.originals[path], self.expected[path] = old, new
            self.modes[path] = entry["mode"]
            if entry["changed"]:
                self.changes[path] = old, new
        self.backup = (
            installation_path(Path(state["backup"]), root) if state["backup"] else None
        )
        if self.backup:
            require(
                self.backup.parent == root / "config/nord-ui-backups"
                and self.backup.name.startswith("nord-ui-"),
                "Invalid backup path",
            )
        self.backup_snapshot = state["backup_snapshot"]
        self.created_dirs = [
            installation_path(Path(p), root) for p in state["created_dirs"]
        ]
        require(
            all(p in (root / "config/nord-ui-backups",) for p in self.created_dirs),
            "Invalid created directory",
        )
        self.attempted = []
        return self

    def validate_bundle(self, bundle):
        self.bundle = Path(bundle)
        catalog = (
            installation_path(self.bundle / "catalog.nord", self.root)
            if self.root in self.bundle.parents
            else self.bundle / "catalog.nord"
        )
        layout = self.bundle / "tools/control-layout.tsv"
        for path in (catalog, layout):
            require(
                path.is_file()
                and not path.is_symlink()
                and path.stat().st_size <= LIMIT,
                "Metadados de páginas dinâmicas ausentes ou inválidos: " + str(path),
            )
        fields = catalog.read_text().split("\0")
        require(
            fields[0] in ("NORD-CATALOG-1", "NORD-LIBRARY-2")
            and fields[-1] == ""
            and (len(fields) - 2) % 7 == 0,
            "Catálogo Nord incompatível",
        )
        entries = [fields[i : i + 7] for i in range(1, len(fields) - 1, 7)]
        require(
            len({entry[0] for entry in entries}) == len(entries),
            "Instrumentos duplicados no catálogo",
        )
        symbols = set()
        world = MetadataWorld(self.bundle)
        try:
            count = world.api("lilv_plugin_get_num_ports", C.c_uint, world.P)(
                world.plugin
            )
            require(count <= 4096, "Quantidade de portas LV2 inválida")
            for index in range(count):
                port = world.api(
                    "lilv_plugin_get_port_by_index", world.P, world.P, C.c_uint
                )(world.plugin, index)
                symbol = world.api("lilv_port_get_symbol", world.P, world.P, world.P)(
                    world.plugin, port
                )
                symbols.add(world.string(symbol))
        finally:
            world.close()
        require("gain" in symbols, "Controle Gain obrigatório ausente no LV2")
        rows = layout.read_text().splitlines()
        require(
            rows and len(rows) <= 4096, "Layout de controladores vazio ou excessivo"
        )
        for line_number, row in enumerate(rows, 1):
            parts = row.split("\t")
            require(
                len(parts) >= 6 and parts[1] and parts[2], "Linha de layout inválida"
            )
            require(
                1 <= int(parts[0]) <= 7
                # The index is global, not the slot within a four-control page.
                and 0 <= int(parts[3]) < 4096
                and parts[4] in symbols,
                "Layout diverge dos controles reais do LV2: linha "
                + str(line_number)
                + ", índice "
                + parts[3]
                + ", símbolo "
                + parts[4],
            )
        policy = {}
        exec(
            compile(NORD_UI_ASSETS["nord_pages.py"], "manager_nord_pages", "exec"),
            policy,
        )
        # Exercise one instrument per motor, plus the fallback, without loading DSPs.
        representatives = {}
        for entry in entries:
            require(
                entry[0] and entry[5] in ("npno", "nsmp4", "pure"),
                "Motor Nord inválido",
            )
            representatives.setdefault(entry[5], entry[0])
        for identity in (*representatives.values(), None):
            pages = policy["controller_pages"](self.bundle, identity)
            require(
                pages
                and all(
                    0 < len(controls) <= 4 and set(controls) <= symbols
                    for _, controls in pages
                ),
                "Páginas incompatíveis com o plugin instalado",
            )

    def apply(self, checkpoint):
        if not self.changes:
            self.verify()
            return
        # Back up every original before the first write; keep successful backups.
        base = self.root / "config/nord-ui-backups"
        for directory in (base,):
            installation_path(directory, self.root)
            if not directory.exists():
                directory.mkdir()
                self.created_dirs.append(directory)
        self.backup = Path(tempfile.mkdtemp(prefix="nord-ui-", dir=base))
        checkpoint()
        try:
            records = []
            for index, (path, (old, new)) in enumerate(self.changes.items()):
                filename = str(index) + ".before" if old is not None else None
                if old is not None:
                    with (self.backup / filename).open("xb") as stream:
                        stream.write(old)
                        stream.flush()
                        os.fsync(stream.fileno())
                records.append(
                    dict(
                        path=str(path),
                        old=filename,
                        installed_sha256=hashlib.sha256(new).hexdigest(),
                        mode=self.modes[path],
                    )
                )
            with (self.backup / "manifest.json").open("x") as stream:
                json.dump(records, stream, indent=2)
                stream.flush()
                os.fsync(stream.fileno())
            self.backup_snapshot = digest_tree(self.backup)
            checkpoint()
            for path in sorted(
                self.changes, key=lambda p: not p.name.startswith("nord_")
            ):
                installation_path(path, self.root)
                old, new = self.changes[path]
                self.attempted.append(path)
                checkpoint()
                self.setup["atomic"](path, new, old, self.modes[path])
                require(
                    path.read_bytes() == new,
                    "Falha na leitura de verificação da integração",
                )
                checkpoint()
            self.verify()
        finally:
            if self.backup_snapshot is None and self.backup is not None:
                self.backup_snapshot = digest_tree(self.backup)
                checkpoint()

    def verify(self):
        for path, expected in self.expected.items():
            installation_path(path, self.root)
            require(
                path.is_file() and path.read_bytes() == expected,
                "Integração não confirmada: " + str(path),
            )
            compile(expected, str(path), "exec")
            if not path.name.startswith("nord_"):
                require(
                    self.setup["patch_text"](expected.decode(), path.stem).encode()
                    == expected,
                    "Hooks dinâmicos inconsistentes",
                )

    def rollback(self):
        failures = []
        ordered = sorted(self.changes, key=lambda p: p.name.startswith("nord_"))
        for path in ordered:
            try:
                installation_path(path, self.root)
                old, expected = self.changes[path]
                current = path.read_bytes() if path.exists() else None
                if current == old:
                    continue
                require(
                    current == expected,
                    "Integração alterada externamente: " + str(path),
                )
                if path.name.startswith("nord_"):
                    for host in ("zynthian_engine_jalv.py", "zynthian_processor.py"):
                        require(
                            "# NORD view hook"
                            not in (self.directory / host).read_text(),
                            "Hooks ainda referenciam módulos; "
                            "preservados para recuperação",
                        )
                if old is None:
                    path.unlink()
                else:
                    self.setup["atomic"](path, old, expected, self.modes[path])
            except Exception as error:
                failures.append(str(error))
        for path, old in self.originals.items():
            try:
                installation_path(path, self.root)
                require(
                    (path.read_bytes() if path.exists() else None) == old,
                    "Configuração anterior não restaurada: " + str(path),
                )
            except Exception as error:
                failures.append(str(error))
        if not failures and self.backup is not None:
            try:
                safe_path(self.backup, self.root)
                require(
                    digest_tree(self.backup) == self.backup_snapshot,
                    "Backup alterado externamente",
                )
                shutil.rmtree(self.backup)
            except Exception as error:
                failures.append(str(error))
        if not failures:
            for directory in reversed(self.created_dirs):
                try:
                    installation_path(directory, self.root)
                    if not any(directory.iterdir()):
                        directory.rmdir()
                except Exception as error:
                    failures.append(str(error))
        return failures
