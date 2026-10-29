#!/usr/bin/env python3
"""Build assets in the DPF bundle; touch no files outside its managed assets."""
import argparse
import fcntl
import shutil
import subprocess
from pathlib import Path
from generate_presets import generate, read_catalog
from control_layout import annotate

MARKER = '# Nord static presets\n'




def assemble_external(catalog_exe, source, bundle):
    if bundle.name != 'Nord.lv2' or bundle.is_symlink():
        raise ValueError('Expected a real Nord.lv2 directory')
    with (bundle / '.nord-refresh.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        _assemble_external_locked(catalog_exe, source, bundle)


def _assemble_external_locked(catalog_exe, source, bundle):
    """Refresh metadata only. Instruments remain in the configured library."""
    if bundle.name != 'Nord.lv2' or bundle.is_symlink():
        raise ValueError('Expected a real Nord.lv2 directory')
    metadata = bundle / 'manifest.ttl'
    if metadata.is_symlink() or '<urn:nord:instrument>' not in metadata.read_text():
        raise ValueError('Not a Nord plugin manifest')
    annotate(bundle)
    base = metadata.read_text(encoding='utf-8').split(MARKER)[0]
    temporary = bundle / 'catalog.nord.tmp'
    subprocess.run([str(catalog_exe), str(source), str(temporary), '--external'], check=True)
    # Generate presets only for readable, registered files. No instrument copies.
    manifest = generate(temporary, bundle)
    temporary.replace(bundle / 'catalog.nord')
    replacement = bundle / 'manifest.ttl.tmp'
    replacement.write_text(base + MARKER + manifest, encoding='utf-8')
    replacement.replace(metadata)
    (bundle / '.nord-managed').write_text('Nord managed bundle\n')
    print(f'[Nord] refreshed {len(read_catalog(bundle / "catalog.nord"))} external instruments; DSP unchanged')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog-exe', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--bundle', type=Path, required=True)
    a = p.parse_args()
    assemble_external(a.catalog_exe.resolve(), a.source.resolve(), a.bundle.absolute())
