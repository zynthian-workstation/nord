#!/usr/bin/env python3
"""Run the audited Oram methods/headless GUI list builder, not just page policy.

Hardware/Tk imports, controller transport and base widgets are stubs. RDF State is parsed by real
Lilv. All setup/apply/restore operations are confined to temporary directories.
"""
import argparse
import ast
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'integrations/zynthian')]
import configure_zynthian_nord_ui as setup
import nord_host
from generate_presets import read_catalog
parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--bundle',type=Path,default=ROOT/'build-v090-zynthian-fixes/bin/Nord.lv2')
parser.add_argument('--fixtures',type=Path,default=ROOT/'tests/fixtures/zynthian-oram-pi-37b776a')
OPTIONS,rest=parser.parse_known_args();sys.argv=[sys.argv[0],*rest]
FIXTURES=OPTIONS.fixtures


def selected(source,classname,methods,base=object):
    tree=ast.parse(source);cls=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name==classname)
    cls.body=[n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in methods]
    cls.bases=[ast.Name(id='Base',ctx=ast.Load())];cls.decorator_list=[]
    ns={'Base':base,'copy':copy,'logging':types.SimpleNamespace(info=lambda *a:None,exception=lambda *a:None),
        'traceback':types.SimpleNamespace(format_exc=lambda:'stub'),'__name__':'zyngine.test_host','__package__':'zyngine'}
    exec(compile(ast.fix_missing_locations(ast.Module(body=[cls],type_ignores=[])),'audited Oram','exec'),ns)
    return ns[classname]


class NordUi(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='nord-ui-');self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);(self.root/'zyngine').mkdir()
        for short,long in [('jalv','zynthian_engine_jalv'),('processor','zynthian_processor')]:
            (self.root/'zyngine'/(long+'.py')).write_bytes((FIXTURES/(short+'.py.txt')).read_bytes())
        self.assets=ROOT/'integrations/zynthian'
        self.bundle=OPTIONS.bundle.resolve();self.entries=read_catalog(self.bundle/'catalog.nord')
        fake=types.ModuleType('zyngine');fake.__path__=[]
        self.modules=patch.dict(sys.modules,{'zyngine':fake,'zyngine.nord_host':nord_host});self.modules.start();self.addCleanup(self.modules.stop)
        changes=setup.plan(self.root,self.assets)
        engine_source=changes[self.root/'zyngine/zynthian_engine_jalv.py'][1].decode()
        processor_source=changes[self.root/'zyngine/zynthian_processor.py'][1].decode()
        self.Engine=selected(engine_source,'zynthian_engine_jalv',['set_preset','cmp_presets'])
        self.Processor=selected(processor_source,'zynthian_processor',['get_ctrl_screens','get_ctrl_screen','build_ctrl_screen',
            'get_state','set_state','set_preset','preload_preset','restore_preset','get_current_screen_index'])
        self.context=patch.object(nord_host,'host_context',side_effect=self.context_for);self.context.start();self.addCleanup(self.context.stop)

    def context_for(self,engine,uri):
        # Both registered factory presets and ordinary user LV2 preset bundles.
        if str(uri).startswith('urn:nord:preset:'):
            identity=str(uri).removeprefix('urn:nord:preset:')
            return self.bundle,self.bundle/'catalog.nord',self.bundle/'presets'/('nord-'+identity+'.ttl')
        document=nord_host.local_path(uri)
        return self.bundle,self.bundle/'catalog.nord',document

    def processor(self):
        engine=self.Engine();engine.plugin_url=nord_host.URI;engine.preset_favs={};engine.commands=[]
        engine.proc_cmd=engine.commands.append;engine.options={'preload':True};engine.nickname='JV/NORD'
        engine.refresh_requests=[];engine.state_manager=types.SimpleNamespace(send_cuia=lambda *args:engine.refresh_requests.append(args))
        p=self.Processor();p.engine=engine;p.ctrl_screens_dict={};p.current_screen_index=99
        # Every actual LV2 port remains in the host model, even hidden/backing ports.
        import re
        symbols=re.findall(r'lv2:symbol "([^"]+)"',(self.bundle/'Nord_dsp.ttl').read_text())
        class Controller:
            def __init__(self,symbol,value):
                self.symbol=symbol;self.value=value;self.group_symbol='nord';self.midi_binding=('keep',symbol)
            def get_state(self):return {'value':self.value}
            def set_value(self,value,*args):self.value=value
            def set_ignore_engine_fb(self,*args):pass
        p.controllers_dict={s:Controller(s,i) for i,s in enumerate(symbols)}
        p.preset_list=[[f"urn:nord:preset:{e['id']}",None,e['name']] for e in self.entries]
        p.preset_info=None;p.preset_name=None;p.preload_info=None;p.preload_index=None;p.preload_name=None
        p.bank_index=0;p.bank_name='factory';p.bank_info=None;p.bank_subdir_info=None;p.preset_subdir_info=None;p.show_fav_presets=False
        p.find_preset_index_by_id=lambda uri:next((i for i,x in enumerate(p.preset_list) if x[0]==uri),None)
        p.get_bank_list=lambda:[];p.load_preset_list=lambda:None
        return p

    def choose(self,p,fmt):
        info=next(x for x in p.preset_list if any(e['format']==fmt and x[0].endswith(e['id']) for e in self.entries))
        self.assertTrue(p.set_preset(info));return info

    def symbols(self,p):return [c.symbol for page in p.get_ctrl_screens().values() for c in page]

    def test_readonly_activation_diagnostics(self):
        before={p:p.read_bytes() for p in (self.root/'zyngine').iterdir()}
        result=setup.diagnose(self.root,self.assets)
        self.assertEqual(result['activation_state'],'inactive');self.assertTrue(result['compatible'])
        self.assertFalse(result['writes_performed']);self.assertEqual(before,{p:p.read_bytes() for p in before})
        backup=setup.apply(setup.plan(self.root,self.assets),self.root/'backups')
        self.assertEqual(setup.diagnose(self.root,self.assets)['activation_state'],'active_on_disk')
        path=self.root/'zyngine/nord_pages.py';path.write_text(path.read_text()+'# outdated asset\n')
        self.assertEqual(setup.diagnose(self.root,self.assets)['activation_state'],'outdated')
        # Restore original installed content before the existing rollback guard.
        path.write_bytes((self.assets/'nord_pages.py').read_bytes());setup.restore(backup)

    def test_page_cache_tracks_updated_layout_without_preset_change(self):
        p=self.processor();info=self.choose(p,'pure');bundle,catalog,document=self.context_for(p.engine,info[0])
        copy_bundle=self.root/'copy.lv2';(copy_bundle/'tools').mkdir(parents=True)
        layout=copy_bundle/'tools/control-layout.tsv';shutil.copy2(bundle/'tools/control-layout.tsv',layout)
        with patch.object(nord_host,'host_context',return_value=(copy_bundle,catalog,document)):
            self.assertIn('General',p.get_ctrl_screens());p.current_screen_index=17
            self.assertIn('General',p.get_ctrl_screens());self.assertEqual(p.current_screen_index,17)
            layout.write_text(layout.read_text().replace('\tGeneral\t','\tGeneral refreshed\t'))
            self.assertIn('General refreshed',p.get_ctrl_screens());self.assertEqual(p.current_screen_index,0)

    def test_factory_state_real_lilv_and_dynamic_three_engines(self):
        p=self.processor();objects=dict(p.controllers_dict);values={s:c.value for s,c in objects.items()}
        for fmt,count in [('npno',5),('nsmp4',111),('pure',122),('npno',5)]:
            self.choose(p,fmt);symbols=self.symbols(p);self.assertEqual(len(symbols),count)
            self.assertEqual(symbols.count('gain'),1)
            self.assertEqual(p.engine.refresh_requests[-1],('REFRESH_SCREEN',['control']))
            self.assertEqual(any(s.startswith('pure_') for s in symbols),fmt=='pure')
            self.assertEqual('kb_touch' in symbols,fmt=='npno')
            self.assertEqual('pitch_bend_up' in symbols,fmt!='npno')
            self.assertEqual('pitch_bend_down' in symbols,fmt!='npno')
            self.assertEqual({s:c.value for s,c in p.controllers_dict.items()},values)
            self.assertTrue(all(p.controllers_dict[s] is c for s,c in objects.items()))
            self.assertTrue(all(len(page)<=4 for page in p.get_ctrl_screens().values()))
            self.assertTrue(0<=p.current_screen_index<len(p.get_ctrl_screens()))

    def test_global_gui_index_is_preserved_when_pages_do_not_change(self):
        p=self.processor();self.choose(p,'pure');self.symbols(p)
        # A real chain view includes header/mixer/other-processor rows too.
        p.current_screen_index=17
        self.assertEqual(len(p.get_ctrl_screens()),35)
        self.assertEqual(p.current_screen_index,17)
        p.engine.set_preset(p,p.preset_info);self.symbols(p)
        self.assertEqual(p.current_screen_index,17)
        self.choose(p,'npno');self.symbols(p);self.assertEqual(p.current_screen_index,0)

    def test_preload_cancel_snapshot_and_zs3_processor_restore(self):
        p=self.processor();p.set_bank_by_info=lambda info:None
        piano=self.choose(p,'npno');self.symbols(p);saved=p.get_state()
        pure=next(i for i,x in enumerate(p.preset_list) if x[0].endswith('builtin:pure'))
        p.preload_preset(pure);self.assertIn('pure_osc1_level',self.symbols(p));self.assertEqual(p.preset_info,piano)
        p.restore_preset();self.assertEqual(len(self.symbols(p)),5)
        # This is the SAME processor.set_state invoked by Oram snapshot/ZS3 paths.
        self.choose(p,'pure')
        for ctrl in p.controllers_dict.values():ctrl.value+=.125
        p.set_state(saved);self.assertEqual(len(self.symbols(p)),5)
        self.assertEqual(p.get_state()['controllers'],saved['controllers'])
        self.assertEqual(set(p.get_state()['controllers']),set(saved['controllers']))

    def test_user_preset_reads_state_not_misleading_title(self):
        p=self.processor();entry=next(e for e in self.entries if e['format']=='npno')
        bundle=self.root/'user espaço.presets.lv2';bundle.mkdir();uri=(bundle/'user.ttl').as_uri()
        (bundle/'manifest.ttl').write_text('@prefix rdfs:<http://www.w3.org/2000/01/rdf-schema#>.\n'
            '<user.ttl> rdfs:seeAlso <user.ttl>.\n')
        (bundle/'user.ttl').write_text('@prefix state:<http://lv2plug.in/ns/ext/state#>.\n'
            '<user.ttl> state:state [<urn:nord:instrument#instrument_id> "'+entry['id']+'"].\n')
        p.set_preset([uri,None,'Pure Synth misleading title']);self.assertEqual(len(self.symbols(p)),5)
        (bundle/'user.ttl').write_text('broken Turtle')
        p.engine.set_preset(p,[uri,None,'Piano']);self.assertEqual(self.symbols(p),['gain'])

    def test_real_oram_gui_fill_list_shows_only_current_pages(self):
        class Base:
            def fill_list(self):pass
        Gui=selected((FIXTURES/'control.py.txt').read_text(),'zynthian_gui_control',['fill_list'],Base)
        p=self.processor();p.engine.name='Jalv/NORD';p.chain=None
        gui=Gui();gui.zyngui=types.SimpleNamespace(get_current_processor=lambda:p)
        gui.configure_processors=lambda cur:setattr(gui,'processors',[cur]);gui.get_screen_info=lambda:None
        for fmt,count in [('npno',2),('nsmp4',32),('pure',35)]:
            self.choose(p,fmt);gui.fill_list()
            displayed=[r for r in gui.list_data if r[0] is not None]
            self.assertEqual(len(displayed),count)
            self.assertEqual([r[2] for r in displayed],list(p.get_ctrl_screens()))

    def test_other_uri_and_unknown_state_do_not_get_wrong_pages(self):
        p=self.processor();p.engine.plugin_url='urn:other';original={'Other':[]};p.ctrl_screens_dict=original
        self.assertIs(p.get_ctrl_screens(),original);self.assertFalse(hasattr(p,'_nord_page_cache'))
        p.engine.plugin_url=nord_host.URI;p.engine.set_preset(p,['urn:unknown',None,'Piano']);self.assertEqual(self.symbols(p),['gain'])

    def test_independent_processors(self):
        a=self.processor();b=self.processor();self.choose(a,'npno');self.choose(b,'pure')
        self.assertEqual(len(self.symbols(a)),5);self.assertEqual(len(self.symbols(b)),122)
        self.choose(b,'nsmp4');self.assertEqual(len(self.symbols(a)),5)

    def test_install_check_idempotency_backup_and_restore(self):
        before={p:p.read_bytes() for p in (self.root/'zyngine').iterdir()}
        changes=setup.plan(self.root,self.assets);self.assertEqual(before,{p:p.read_bytes() for p in (self.root/'zyngine').iterdir()})
        backup=setup.apply(changes,self.root/'backups');self.assertIsNotNone(backup)
        self.assertEqual(setup.plan(self.root,self.assets),{})
        setup.restore(backup);self.assertEqual(before,{p:p.read_bytes() for p in (self.root/'zyngine').iterdir()})

    def test_apply_failure_after_replace_rolls_back_every_file(self):
        before={p:p.read_bytes() for p in (self.root/'zyngine').iterdir()}
        real=setup.atomic;calls=[]
        def failure(path,*args,**kwargs):
            real(path,*args,**kwargs);calls.append(path)
            if len(calls)==2:raise OSError('simulated post-replace fsync failure')
        with patch.object(setup,'atomic',side_effect=failure):
            with self.assertRaisesRegex(OSError,'post-replace'):setup.apply(setup.plan(self.root,self.assets),self.root/'backups')
        self.assertEqual(before,{p:p.read_bytes() for p in (self.root/'zyngine').iterdir()})

    def test_modified_host_refused_and_concurrent_restore_refused(self):
        target=self.root/'zyngine/zynthian_processor.py';old=target.read_text()
        target.write_text(old.replace('return self.ctrl_screens_dict','return {}'))
        with self.assertRaisesRegex(ValueError,'Unsupported'):setup.plan(self.root,self.assets)
        target.write_text(old)
        engine=self.root/'zyngine/zynthian_engine_jalv.py';engine_old=engine.read_text()
        inserted=setup.INSERTIONS[('zynthian_engine_jalv','set_preset')][1]
        engine.write_text(engine_old.replace('    def cmp_presets(self, preset1, preset2):',
            '    def cmp_presets(self, preset1, preset2):\n'+inserted,1))
        with self.assertRaisesRegex(ValueError,'outside'):setup.plan(self.root,self.assets)
        engine.write_text(engine_old)
        backup=setup.apply(setup.plan(self.root,self.assets),self.root/'backups')
        target.write_text(target.read_text()+'# external edit\n');raw=target.read_bytes()
        with self.assertRaises(RuntimeError):setup.restore(backup)
        self.assertEqual(target.read_bytes(),raw)


if __name__=='__main__':unittest.main(verbosity=2)
