#!/usr/bin/env python3
"""Prepare/apply ONLY audited Nord view hooks; never SSH or restart the host.

Separate explicit action from LV2 deployment: stock Oram has no conditional-page
extension hook. --check is read-only, --apply backs up all four files and rolls
back synchronous failures. --restore refuses to overwrite a concurrent edit.
"""
import argparse
import ast
import difflib
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile

URI='urn:nord:instrument'
MARKER='# Nord managed controller integration v1'
# Canonical hashes are filled from the pinned upstream Oram fixture.
PROFILES = {('zynthian_engine_jalv', 'set_preset'): '88ff59cf11b7b0e40183b774c7e468a2127bbde55acb5db34e472425f8064fcc', ('zynthian_processor', 'get_ctrl_screens'): '69aad787af0b3d1e20773eca30478161307e08876c43e71c35720133a6b2a61b', ('zynthian_processor', 'get_ctrl_screen'): '891aa620425f317ab99ff36303620f3939e56ad109eae0c5c0ff55a324186d40'}
INSERTIONS={
 ('zynthian_engine_jalv','set_preset'):('        return True', '''        # NORD view hook begin: preset
        if self.plugin_url == "urn:nord:instrument":
            from .nord_host import preset_changed
            preset_changed(processor, preset)
        # NORD view hook end: preset
'''),
 ('zynthian_processor','get_ctrl_screens'):('        return self.ctrl_screens_dict', '''        # NORD view hook begin: screens
        if getattr(self.engine, "plugin_url", None) == "urn:nord:instrument":
            from .nord_host import screens
            return screens(self)
        # NORD view hook end: screens
'''),
 ('zynthian_processor','get_ctrl_screen'):('        try:', '''        # NORD view hook begin: screen
        if getattr(self.engine, "plugin_url", None) == "urn:nord:instrument":
            self.get_ctrl_screens()
        # NORD view hook end: screen
''')}


def fingerprint(node):
    def canonical(value):
        if isinstance(value,ast.AST):
            return dict(kind=type(value).__name__,fields={k:canonical(v) for k,v in ast.iter_fields(value) if v is not None and v!=[]})
        if isinstance(value,list):return [canonical(v) for v in value]
        return value
    return hashlib.sha256(json.dumps(canonical(node),sort_keys=True,separators=(',',':')).encode()).hexdigest()


def patch_text(text,clsname):
    original=text
    for (cls,method),(anchor,insert) in INSERTIONS.items():
        if cls!=clsname:continue
        if insert in text:
            tree=ast.parse(text)
            target=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name==clsname)
            node=next(n for n in target.body if isinstance(n,ast.FunctionDef) and n.name==method)
            if text.count(insert)!=1 or insert not in ast.get_source_segment(text,node):
                raise ValueError('Nord hook outside its audited method')
            text=text.replace(insert,'')
    # Reject altered/incomplete old hooks, not just an arbitrary marker.
    if '# NORD view hook' in text:raise ValueError('Incomplete/modified Nord hook')
    tree=ast.parse(text);cls=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name==clsname)
    lines=text.splitlines(keepends=True);edits=[]
    for (name,method),(anchor,insert) in INSERTIONS.items():
        if name!=clsname:continue
        node=next(n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name==method)
        if fingerprint(node)!=PROFILES[(name,method)]:raise ValueError('Unsupported Oram method: '+name+'.'+method)
        segment=''.join(lines[node.lineno-1:node.end_lineno])
        if segment.count(anchor)!=1:raise ValueError('Ambiguous hook anchor')
        edits.append((node.lineno-1,node.end_lineno,segment.replace(anchor,insert+anchor)))
    for start,end,new in sorted(edits,reverse=True):lines[start:end]=[new]
    result=''.join(lines);compile(result,'Nord Oram integration','exec')
    return result


def safe(path):
    path=Path(path).absolute()
    if '..' in path.parts or any(p.is_symlink() for p in (path,*path.parents)):
        raise ValueError('Symlink/traversal refused: '+str(path))
    return path


def plan(ui_root,assets):
    directory=safe(ui_root)/'zyngine'
    if not directory.is_dir():raise ValueError('Missing Zynthian zyngine directory')
    changes={}
    for cls in ('zynthian_engine_jalv','zynthian_processor'):
        path=safe(directory/(cls+'.py'));old=path.read_bytes()
        new=patch_text(old.decode('utf-8'),cls).encode('utf-8')
        changes[path]=(old,new)
    for name in ('nord_host.py','nord_pages.py'):
        path=safe(directory/name);new=(assets/name).read_bytes()
        if MARKER.encode() not in new:raise ValueError('Unmanaged integration asset')
        old=path.read_bytes() if path.exists() else None
        if old is not None and MARKER.encode() not in old:raise ValueError('Refusing unrelated '+str(path))
        compile(new,str(path),'exec');changes[path]=(old,new)
    return {p:pair for p,pair in changes.items() if pair[0]!=pair[1]}


def atomic(path,data,expected,mode=0o644):
    path=safe(path)
    previous=path.stat() if path.exists() else None
    fd,name=tempfile.mkstemp(prefix='.nord-ui-',dir=path.parent)
    try:
        with os.fdopen(fd,'wb') as output:
            if previous:os.fchown(output.fileno(),previous.st_uid,previous.st_gid)
            os.fchmod(output.fileno(),stat.S_IMODE(previous.st_mode) if previous else mode)
            output.write(data);output.flush();os.fsync(output.fileno())
        current=path.read_bytes() if path.exists() else None
        if current!=expected:raise RuntimeError('File changed concurrently: '+str(path))
        os.replace(name,path)
        directory=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY)
        try:os.fsync(directory)
        finally:os.close(directory)
    finally:
        if Path(name).exists():Path(name).unlink()


def apply(changes,backup_root):
    if not changes:return None
    root=safe(backup_root);root.mkdir(parents=True,exist_ok=True)
    folder=Path(tempfile.mkdtemp(prefix='nord-ui-',dir=root));manifest=[];written=[]
    # Save every original before writing any file.
    for index,(path,(old,new)) in enumerate(changes.items()):
        record=dict(path=str(path),old=str(index)+'.before' if old is not None else None,
            installed_sha256=hashlib.sha256(new).hexdigest(),mode=stat.S_IMODE(path.stat().st_mode) if old is not None else 0o644)
        if old is not None:(folder/record['old']).write_bytes(old)
        manifest.append(record)
    (folder/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    try:
        # Install the modules first, then the hooks. No running process is reloaded.
        ordered=sorted(changes,key=lambda p:0 if p.name.startswith('nord_') else 1)
        for path in ordered:
            old,new=changes[path];written.append(path);atomic(path,new,old)
            if path.read_bytes()!=new:raise RuntimeError('UI hook read-back mismatch')
    except BaseException:
        for path in reversed(written):
            old,new=changes[path]
            if not path.exists() or path.read_bytes()!=new:continue # Never overwrite an unrelated concurrent writer.
            if old is None:path.unlink()
            else:atomic(path,old,new)
        raise
    return folder


def restore(folder):
    folder=safe(folder);records=json.loads((folder/'manifest.json').read_text())
    for record in records:
        path=safe(record['path'])
        if hashlib.sha256(path.read_bytes()).hexdigest()!=record['installed_sha256']:
            raise RuntimeError('Refusing restore over changed file: '+str(path))
        if record['old'] is not None:
            name=record['old']
            if Path(name).name!=name:raise ValueError('Unsafe backup filename')
            compile((folder/name).read_bytes(),str(path),'exec')
    # Remove hooks before their modules, so even a concurrent UI start can import.
    ordered=sorted(records,key=lambda r:Path(r['path']).name.startswith('nord_'))
    for record in ordered:
        path=safe(record['path']);current=path.read_bytes()
        if record['old'] is None:path.unlink()
        else:atomic(path,(folder/record['old']).read_bytes(),current,record['mode'])



def diagnose(ui_root,assets):
    """Read-only installation state; a file hook is not proof of runtime import."""
    root=safe(ui_root);directory=root/'zyngine';hooks={};modules={}
    for (cls,method),(_,insertion) in INSERTIONS.items():
        path=safe(directory/(cls+'.py'))
        text=path.read_text() if path.is_file() else ''
        hooks[cls+'.'+method]=insertion in text
    for name in ('nord_host.py','nord_pages.py'):
        path=safe(directory/name);expected=(assets/name).read_bytes()
        actual=path.read_bytes() if path.is_file() else None
        modules[name]=dict(installed=actual is not None,matches_bundle=actual==expected,
            installed_sha256=hashlib.sha256(actual).hexdigest() if actual is not None else None,
            bundle_sha256=hashlib.sha256(expected).hexdigest())
    try:
        changes=plan(root,assets);compatible=True;error=None
    except (ValueError,StopIteration,SyntaxError,OSError) as failure:
        changes={};compatible=False;error=str(failure)
    count=sum(hooks.values())
    state='unsupported' if not compatible else 'inactive' if count==0 else 'partial' if count!=len(hooks) else 'outdated' if changes else 'active_on_disk'
    try:revision=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],stderr=subprocess.DEVNULL,text=True).strip()
    except (OSError,subprocess.CalledProcessError):revision=None
    return dict(uri=URI,ui_root=str(root),revision=revision,compatible=compatible,
        activation_state=state,hooks=hooks,modules=modules,error=error,
        files_requiring_update=[str(p) for p in changes],runtime_loaded='not_verified',writes_performed=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    flags=parser.add_mutually_exclusive_group(required=True)
    flags.add_argument('--check',action='store_true');flags.add_argument('--apply',action='store_true');flags.add_argument('--restore',type=Path)
    parser.add_argument('--json',action='store_true',help='Read-only installation/compatibility report (requires --check)')
    parser.add_argument('--ui-root',type=Path,default=Path('/zynthian/zynthian-ui'))
    parser.add_argument('--backup-dir',type=Path,default=Path('/zynthian/config/nord-ui-backups'))
    args=parser.parse_args()
    if args.json and not args.check:parser.error('--json requires --check')
    if args.restore:restore(args.restore);return
    here=Path(__file__).resolve().parent
    assets=here if (here/'nord_host.py').is_file() else here.parent/'integrations/zynthian'
    if args.json:
        print(json.dumps(diagnose(args.ui_root,assets),ensure_ascii=False,indent=2));return
    changes=plan(args.ui_root,assets)
    for path,(old,new) in changes.items():
        print(''.join(difflib.unified_diff((old or b'').decode().splitlines(True),new.decode().splitlines(True),fromfile=str(path),tofile=str(path))),end='')
    if args.apply:print('Backup:',apply(changes,args.backup_dir))
    else:print('Read-only audit; no files modified.')


if __name__=='__main__':main()
