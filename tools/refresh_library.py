#!/usr/bin/env python3
"""Refresh Nord metadata/presets without rebuilding DSP or copying instruments."""
import argparse
import os
import shutil
from pathlib import Path
from assemble_bundle import assemble_external

def data_home():
    if os.environ.get('ZYNTHIAN_MY_DATA_DIR'):
        return Path(os.environ['ZYNTHIAN_MY_DATA_DIR']) / 'nord-lv2'
    return Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'nord-lv2'

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--metadata', type=Path, default=data_home())
    parser.add_argument('--bundle', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--catalog-exe', type=Path, default=Path(__file__).resolve().parent/'nord_catalog')
    args = parser.parse_args()
    source = args.root or Path(os.environ.get('NORD_LIBRARY_ROOT', data_home()/'instruments'))
    if not source.is_dir():
        raise SystemExit(f'Instrument library unavailable: {source}; use --root')
    assemble_external(args.catalog_exe.resolve(), source.resolve(), args.bundle.absolute())
    args.metadata.mkdir(parents=True, exist_ok=True)
    target = args.metadata / 'catalog.nord'
    if target.resolve() != (args.bundle / 'catalog.nord').resolve():
        temporary = target.with_suffix('.nord.tmp')
        shutil.copy2(args.bundle / 'catalog.nord', temporary)
        temporary.replace(target)
    print(f'[Nord] persistent catalog: {target}; reinstantiate plugin after host preset rescan')
